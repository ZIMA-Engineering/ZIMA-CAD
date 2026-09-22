#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <zima/drawing/model_annotations.hpp>
#include <zima/kernel/stable_id.hpp>
using namespace zima;
void require(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Invalid input accepted");
}
int main() {
  try {
    drawing::ModelAnnotationSource source;
    source.document_id = "source-part";
    source.instance_path = "assembly/first";
    kernel::ViewerDimension dimension;
    dimension.reference = {"sketch", "dimension:width", source.instance_path};
    dimension.witness_first = {0, 0, 0};
    dimension.witness_second = {10, 0, 0};
    dimension.line_first = {0, 5, 0};
    dimension.line_second = {10, 5, 0};
    dimension.value = 10;
    source.dimensions.push_back(dimension);
    {
      auto original=dimension;original.plane_normal={0,-1,0};original.line_first={0,0,8};original.line_second={10,0,8};
      drawing::ModelAnnotation annotation;annotation.model_dimension=original;
      drawing::DrawingView projected;projected.camera={{-1,0,0},{0,1,0},{0,0,1}};
      const auto shown=drawing::drawing_model_dimension(projected,annotation);
      require(std::abs(shown.line_first.y)==8&&shown.line_first.z==0&&shown.value==10,"Model dimension did not rotate into drawing plane");
      require(annotation.model_dimension->line_first==original.line_first&&annotation.model_dimension->plane_normal==original.plane_normal,"Drawing projection changed source dimension");
      const double q=std::sqrt(.5);projected.camera={{-q,-q,0},{-q,q,0},{0,0,1}};
      const auto rotated=drawing::drawing_model_dimension(projected,annotation);
      require(rotated.value==10&&std::abs(rotated.line_first.y)==8,"Rotated view lost model dimension geometry");
    }
    kernel::ViewerEdge circle;
    circle.construction = true;
    circle.reference = {"sketch", "curve:circle", source.instance_path};
    for (int i = 0; i <= 48; ++i) {
      double angle = i * 6.283185307179586 / 48;
      circle.points.push_back({std::cos(angle) * 3, std::sin(angle) * 3, 0});
    }
    source.construction.push_back(circle);
    kernel::ViewerAxis axis;
    axis.reference = {"axis-container", "axis", source.instance_path};
    axis.direction = {1, 0, 0};
    axis.display_length = 40;
    source.axes.push_back(axis);
    auto second = source;
    second.instance_path = "assembly/second";
    for (auto &d : second.dimensions)
      d.reference.instance_path = second.instance_path;
    for (auto &e : second.construction)
      e.reference.instance_path = second.instance_path;
    for (auto &a : second.axes)
      a.reference.instance_path = second.instance_path;
    std::vector sources{source, second};
    drawing::DrawingView view;
    view.id = "front";
    view.source_document_id = "assembly";
    view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    drawing::refresh_model_annotations(view, sources);
    require(view.model_annotations.size() == 6,
            "Repeated Part occurrences collapsed");
    require(view.model_annotations[1].curves[0].size() == 49,
            "Construction circle lost its finite curve");
    require(source.construction[0].points.size() == 49 &&
                source.construction[0].reference.semantic_key == "curve:circle",
            "Projection modified source geometry");
    const auto original = view.model_annotations;
    auto other = view;
    other.id = "other";
    drawing::ShowEraseSession session(view);
    std::set kinds{drawing::ModelAnnotationKind::Dimension,
                   drawing::ModelAnnotationKind::Axis,
                   drawing::ModelAnnotationKind::Construction};
    auto ids = session.candidates(drawing::ShowEraseMode::Show, kinds);
    require(ids.size() == 6, "Hidden items not offered");
    {
      auto with_origins = view;
      for (const auto* owner : {"source-part:origin", "body-origin", "assembly:origin"})
        for (const auto* path : {"assembly/first", "assembly/second"})
          for (const auto* semantic : {"origin:axis:x", "origin:axis:y", "origin:axis:z", "sketch_axis:x", "sketch_axis:y", "axis:x", "axis:y", "axis:z"}) {
            auto origin = view.model_annotations[2];
            origin.source = {"source-part", owner, semantic, path};
            with_origins.model_annotations.push_back(origin);
          }
      drawing::ShowEraseSession show_origins(with_origins);
      require(show_origins.candidates(drawing::ShowEraseMode::Show, kinds) == ids,
              "Show offered Part, Body or Assembly Origin axes");
      for (auto& item : with_origins.model_annotations) {
        item.visible = true;
        if(drawing::origin_annotation(item.source))require(!drawing::project_model_annotation(with_origins,item).visible,"Stored visible Origin was rendered");
      }
      const auto loaded=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(with_origins.model_annotations));
      require(std::ranges::none_of(loaded,[](const auto& item){return drawing::origin_annotation(item.source)&&item.visible;}),"Saved Origin axes remained visible after reopening");
      drawing::ShowEraseSession erase_origins(with_origins);
      require(erase_origins.candidates(drawing::ShowEraseMode::Erase, kinds) == ids,
              "Erase offered Origin axes or excluded geometric axes");
      rejects([&] {
        erase_origins.preview(drawing::ShowEraseMode::Erase,
                              drawing::ShowEraseSelection::RemoveSelected, kinds,
                              {with_origins.model_annotations.back().source});
      });
    }
    auto pending = session.preview(drawing::ShowEraseMode::Show,
                                   drawing::ShowEraseSelection::KeepSelected,
                                   kinds, {ids[0], ids[1]});
    require(pending[0].visible && pending[1].visible && !pending[3].visible,
            "Show failed occurrence isolation");
    require(view.model_annotations == original,
            "Preview modified view before OK");
    view.model_annotations = pending;
    require(other.model_annotations == original,
            "Visibility leaked between views");
    drawing::ShowEraseSession erase(view);
    require(erase.candidates(drawing::ShowEraseMode::Erase, kinds).size() == 2,
            "Erase offered hidden items");
    auto erased = erase.preview(drawing::ShowEraseMode::Erase,
                                drawing::ShowEraseSelection::RemoveSelected,
                                kinds, {ids[0]});
    require(!erased[0].visible && erased[1].visible,
            "Erase affected unrelated visible item");
    rejects([&] {
      erase.preview(drawing::ShowEraseMode::Erase,
                    kinds.empty() ? drawing::ShowEraseSelection::KeepSelected
                                  : drawing::ShowEraseSelection::RemoveSelected,
                    kinds, {ids[3]});
    });
    view.model_annotations[0].paper_handles = {{"text", {23, 14}},
                                               {"arrow_first", {1, 12}},
                                               {"arrow_second", {16, 12}}};
    auto handles = view.model_annotations[0].paper_handles;
    view.scale = .25;
    source.dimensions[0].value = 18;
    sources[0] = source;
    drawing::refresh_model_annotations(view, sources);
    require(view.model_annotations[0].value == 18 &&
                view.model_annotations[0].paper_handles == handles &&
                view.model_annotations[0].visible,
            "Regeneration lost paper handles or visibility");
    sources.erase(sources.begin());
    drawing::refresh_model_annotations(view, sources);
    require(view.model_annotations[0].unresolved &&
                !view.model_annotations[3].unresolved,
            "Missing source silently replaced by repeated occurrence");
    sources.insert(sources.begin(), source);
    drawing::refresh_model_annotations(view, sources);
    require(!view.model_annotations[0].unresolved &&
                view.model_annotations[0].paper_handles == handles,
            "Returning identity lost presentation");
    auto unchanged = view.model_annotations;
    sources.push_back(source);
    rejects([&] { drawing::refresh_model_annotations(view, sources); });
    require(view.model_annotations == unchanged,
            "Invalid refresh partially modified view");
    require(drawing::deserialize_model_annotations(
                drawing::serialize_model_annotations(view.model_annotations)) ==
                view.model_annotations,
            "Annotation roundtrip");
    auto oriented = view;
    const std::set dimension_kind{drawing::ModelAnnotationKind::Dimension};
    auto &oriented_dimension = oriented.model_annotations[0];
    oriented_dimension.visible = false;
    const auto offered = [&] {
      const auto candidates = drawing::ShowEraseSession(oriented).candidates(
          drawing::ShowEraseMode::Show, dimension_kind);
      return std::find(candidates.begin(), candidates.end(),
                       oriented_dimension.source) != candidates.end();
    };
    require(offered(), "Face-on dimension not offered");
    oriented.camera.depth = {0, 0, -2};
    require(offered(), "Reverse view lost face-on dimension");
    oriented.camera.depth = {1, 0, 0};
    require(offered(), "Edge-on dimension disappeared");
    oriented.camera.depth = {1, 0, 1};
    require(offered(), "Oblique dimension disappeared");
    oriented.camera.depth = {0, 0, 1};
    oriented_dimension.plane_normal = {0, 2, 0};
    require(offered(), "Rotated occurrence dimension disappeared");
    oriented.camera.depth = {0, -1, 0};
    require(offered(), "Rotated occurrence dimension missing in matching view");
    oriented_dimension.visible = true;
    oriented.camera.depth = {0, 0, 1};
    require(!drawing::ShowEraseSession(oriented)
                .candidates(drawing::ShowEraseMode::Erase, dimension_kind)
                .empty(),
            "Oblique stored dimension offered for Erase");
    require(oriented_dimension.visible &&
                oriented_dimension.paper_handles == handles,
            "Orientation filtering lost stored visibility or handles");
    require(drawing::deserialize_model_annotations(
                drawing::serialize_model_annotations(
                    oriented.model_annotations)) == oriented.model_annotations,
            "Dimension plane normal lost on reopen");
    auto document = drawing::DrawingDocument::create_default();
    document.sheets[0].views = {view, other};
    auto path = std::filesystem::temp_directory_path() /
                (kernel::make_stable_id() + ".drwz");
    document.save(path);
    auto loaded = drawing::DrawingDocument::load(path);
    std::filesystem::remove(path);
    require(loaded.sheets[0].views[0].model_annotations ==
                    view.model_annotations &&
                loaded.sheets[0].views[1].model_annotations ==
                    other.model_annotations,
            "Drawing per-view persistence");
    std::cout << "Show/Erase identities, projections, view/occurrence "
                 "isolation, preview and persistence passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
