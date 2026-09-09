#include "drawing_annotation_source.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/workspace/workspace.hpp>
namespace zima::app {
namespace {
struct AnnotationTransform {
  assembly::ComponentPlacement placement;
  std::optional<kernel::MirrorPlane> mirror;
  std::optional<std::pair<kernel::PatternRequest, unsigned>> pattern;
};
void transform_annotations(kernel::ViewerMesh &mesh,
                           const AnnotationTransform &transform) {
  const auto point = [&](kernel::Vec3 p) {
    return transform.mirror ? kernel::mirrored_point(p, *transform.mirror)
                            : kernel::pattern_point(p, transform.pattern->first,
                                                    transform.pattern->second);
  };
  const auto vector = [&](kernel::Vec3 p) {
    return transform.mirror
               ? kernel::mirrored_vector(p, *transform.mirror)
               : kernel::pattern_vector(p, transform.pattern->first,
                                        transform.pattern->second);
  };
  for (auto &p : mesh.vertices)
    p = point(p);
  for (auto &edge : mesh.edges)
    for (auto &p : edge.points)
      p = point(p);
  for (auto &axis : mesh.axes) {
    axis.point = point(axis.point);
    axis.direction = vector(axis.direction);
  }
  for (auto &d : mesh.dimensions) {
    d.witness_first = point(d.witness_first);
    d.witness_second = point(d.witness_second);
    d.line_first = point(d.line_first);
    d.line_second = point(d.line_second);
    d.plane_normal = vector(d.plane_normal);
    if (transform.mirror)
      d.plane_normal = {-d.plane_normal.x, -d.plane_normal.y,
                        -d.plane_normal.z};
  }
}
} // namespace

std::vector<drawing::ModelAnnotationSource>
drawing_annotation_sources(workspace::Workspace *workspace,
                           const std::string &root,
                           const std::filesystem::path &root_path) {
  std::vector<drawing::ModelAnnotationSource> result;
  std::set<std::string> stack;
  const auto append = [](kernel::ViewerMesh &target,
                         kernel::ViewerMesh source) {
    target.dimensions.insert(target.dimensions.end(), source.dimensions.begin(),
                             source.dimensions.end());
    target.edges.insert(target.edges.end(), source.edges.begin(),
                        source.edges.end());
    target.axes.insert(target.axes.end(), source.axes.begin(),
                       source.axes.end());
  };
  std::function<void(std::string, std::filesystem::path, assembly::InstancePath,
                     std::vector<AnnotationTransform>)>
      visit;
  visit = [&](std::string id, std::filesystem::path path,
              assembly::InstancePath occurrence,
              std::vector<AnnotationTransform> placements) {
    if (!stack.insert(id).second)
      throw std::runtime_error("Cyclic drawing annotation source");
    kernel::ViewerMesh mesh;
    const auto part_mesh = [&](const document::PartDocument &part,
                               const kernel::ViewerMesh &calculated) {
      if (part.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      append(mesh, part.construction_viewer_mesh());
      mesh.axes.insert(mesh.axes.end(),
                       calculated.original_references.axes.begin(),
                       calculated.original_references.axes.end());
      mesh.axes.insert(mesh.axes.end(), calculated.axes.begin(),
                       calculated.axes.end());
      mesh.dimensions.insert(mesh.dimensions.end(),
                             calculated.dimensions.begin(),
                             calculated.dimensions.end());
      for (const auto &sketch : part.sketches) {
        if (sketch.suppressed)
          continue;
        if (const auto *owner = part.find_container(sketch.owner_container_id);
            owner && owner->suppressed)
          continue;
        auto packet = sketch.viewer_mesh();
        if (const auto *body = part.body_owner_for_object(sketch.id))
          packet = part.place_body_mesh(std::move(packet), body->scope.id);
        append(mesh, std::move(packet));
      }
    };
    const auto assembly_mesh = [&](const assembly::AssemblyDocument &assembly) {
      if (assembly.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      append(mesh, assembly.construction_viewer_mesh());
      const auto suppressed = assembly.effectively_suppressed_occurrences();
      std::function<void(
          const assembly::PartOccurrence &, assembly::InstancePath,
          std::vector<AnnotationTransform>, std::set<std::string>)>
          expand;
      expand = [&](const auto &component, auto target_path, auto chain,
                   auto copied) {
        if (!copied.insert(component.occurrence_id).second)
          throw std::runtime_error("Cyclic derived annotation source");
        chain.push_back({component.placement, {}, {}});
        if (component.derived_copy) {
          const auto *source =
              assembly.find_occurrence(component.derived_copy->source_id);
          if (!source)
            throw std::runtime_error("Missing derived annotation source");
          if (component.derived_copy->pattern) {
            const auto pattern =
                kernel::validated_pattern(*component.derived_copy->pattern);
            for (unsigned index = 1;
                 index < kernel::pattern_instance_count(pattern); ++index) {
              auto next = chain;
              next.push_back({{}, {}, std::pair{pattern, index}});
              expand(*source,
                     target_path.child(kernel::pattern_copy_id(pattern, index)),
                     std::move(next), copied);
            }
          } else {
            chain.push_back({{},
                             kernel::normalized_mirror_plane(
                                 component.derived_copy->resolved_plane),
                             {}});
            expand(*source, target_path, std::move(chain), copied);
          }
          return;
        }
        auto child_path = component.source_path;
        if (child_path.is_relative())
          child_path = path.parent_path() / child_path;
        visit(component.source_document_id, child_path, target_path,
              std::move(chain));
      };
      for (const auto &component : assembly.components) {
        if (suppressed.contains(component.occurrence_id) || !component.visible)
          continue;
        expand(component, occurrence.child(component.occurrence_id), placements,
               {});
      }
    };
    if (workspace && workspace->open_part(id))
      part_mesh(workspace->open_part(id)->session.document(),
                workspace->authoritative_viewer_mesh(id));
    else if (workspace && workspace->open_assembly(id))
      assembly_mesh(workspace->open_assembly(id)->session.document());
    else if (path.extension() == ".prtz") {
      std::vector<kernel::BodyResult> boundaries;
      auto part = document::PartDocument::load(path, &boundaries);
      part_mesh(part, boundaries.empty() ? kernel::ViewerMesh{}
                                         : boundaries.back().mesh);
    } else if (path.extension() == ".asmz")
      assembly_mesh(assembly::AssemblyDocument::load(path));
    else
      throw std::runtime_error("Zdroj anotací není dostupný: " + path.string());
    std::erase_if(mesh.dimensions,
                  [](const auto &d) { return !d.reference.valid(); });
    std::erase_if(mesh.edges, [](const auto &e) {
      return !e.reference.valid() || !e.construction;
    });
    std::erase_if(mesh.axes,
                  [](const auto &a) { return !a.reference.valid(); });
    const auto unique = [](auto &values) {
      std::set<std::pair<std::string, std::string>> seen;
      std::erase_if(values, [&](const auto &value) {
        return !seen.emplace(value.reference.owner_id,
                             value.reference.semantic_key)
                    .second;
      });
    };
    unique(mesh.axes);
    unique(mesh.dimensions);
    unique(mesh.edges);
    // Consume the existing Assembly display transformation. Extra vertices
    // carry optional text anchors through exactly the same placement, without
    // solving.
    for (const auto &d : mesh.dimensions)
      mesh.vertices.push_back(d.label_position.value_or(d.line_second));
    for (auto i = placements.rbegin(); i != placements.rend(); ++i) {
      if (i->mirror || i->pattern) {
        transform_annotations(mesh, *i);
        continue;
      }
      assembly::AssemblyDocument carrier;
      assembly::PartOccurrence c;
      c.occurrence_id = "annotation-transform";
      c.source_document_id = id;
      c.source_kind = assembly::ComponentSourceKind::Assembly;
      c.placement = i->placement;
      // build_scene also adds the carrier's own datums. Keep only the
      // source occurrence, including its anchor vertices, after transformation.
      for (auto &d : mesh.dimensions)
        d.reference.instance_path.clear();
      for (auto &e : mesh.edges)
        e.reference.instance_path.clear();
      for (auto &a : mesh.axes)
        a.reference.instance_path.clear();
      const auto anchors = mesh.vertices.size();
      const auto source_path =
          assembly::InstancePath{}.child(c.occurrence_id).encoded();
      c.calculated_source.mesh = std::move(mesh);
      carrier.components.push_back(std::move(c));
      auto transformed = carrier.build_scene();
      const auto foreign = [&](const auto &value) {
        return value.reference.instance_path != source_path;
      };
      std::erase_if(transformed.dimensions, foreign);
      std::erase_if(transformed.edges, foreign);
      std::erase_if(transformed.axes, foreign);
      mesh = {};
      mesh.vertices.assign(transformed.vertices.end() - anchors,
                           transformed.vertices.end());
      mesh.dimensions = std::move(transformed.dimensions);
      mesh.edges = std::move(transformed.edges);
      mesh.axes = std::move(transformed.axes);
    }
    for (std::size_t i = 0; i < mesh.dimensions.size(); ++i) {
      mesh.dimensions[i].label_position = mesh.vertices.at(i);
      mesh.dimensions[i].reference.instance_path = occurrence.encoded();
    }
    for (auto &e : mesh.edges)
      e.reference.instance_path = occurrence.encoded();
    for (auto &a : mesh.axes)
      a.reference.instance_path = occurrence.encoded();
    result.push_back({id, occurrence.encoded(), std::move(mesh.dimensions),
                      std::move(mesh.edges), std::move(mesh.axes)});
    stack.erase(id);
  };
  visit(root, root_path, {}, {});
  return result;
}
} // namespace zima::app
