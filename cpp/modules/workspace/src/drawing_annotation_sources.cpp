#include <zima/document/object_annotation_frames.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <algorithm>
#include <cctype>
#include <zima/document/file_path.hpp>
#include <functional>
#include <stdexcept>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
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
drawing_annotation_sources(const Workspace *workspace,
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
    kernel::ModelEnvelope envelope;
    std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope> frames;
    std::vector<kernel::DimensionLayoutEntry> layouts;
    std::map<std::pair<std::string,std::string>,kernel::ModelEnvelope> axis_frames;
    const auto part_mesh = [&](const document::PartDocument &part,
                               const kernel::ViewerMesh &calculated) {
      if (part.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      envelope=kernel::model_envelope(calculated);frames=document::part_annotation_envelopes(part,calculated);layouts=part.dimension_layouts;
      append(mesh, part.construction_viewer_mesh());
      append(mesh, part.origin_viewer_mesh());
      frames[{part.document_id+":origin",{}}]=envelope;
      for(const auto& body:part.body_history.bodies()) {
        document::PartDocument carrier;carrier.document_id=body.scope.id;
        auto origin=carrier.origin_viewer_mesh();
        for(auto& axis:origin.axes)axis.reference.owner_id=body.origin().id;
        append(mesh,part.place_body_mesh(std::move(origin),body.scope.id));
        if(auto found=frames.find({body.scope.id,{}});found!=frames.end())
            frames[{body.origin().id,{}}]=found->second;
      }
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
        layouts.insert(layouts.begin(),sketch.dimension_layouts.begin(),sketch.dimension_layouts.end());
        auto packet = sketch.viewer_mesh();
        if (const auto *body = part.body_owner_for_object(sketch.id))
          packet = part.place_body_mesh(std::move(packet), body->scope.id);
        frames=kernel::object_envelopes(packet,std::move(frames));
        // Circular profile axes use the bounds of that cylinder, not the box
        // spanning every hole of the owning extrusion. Consume persisted Sketch
        // geometry and calculated owner bounds; no body calculation is needed.
        for(const auto& circle:sketch.circles) {
          if(circle.construction || circle.radius<=0)continue;
          const auto* center=sketch.find_point(circle.center_point_id);
          if(!center)continue;
          const auto c=sketch.world_point(center->x,center->y);
          kernel::ViewerMesh basis;
          basis.vertices={c,kernel::dimension_add(c,sketch.x_axis()),
              kernel::dimension_add(c,sketch.y_axis()),kernel::dimension_add(c,sketch.normal())};
          if(const auto* body=part.body_owner_for_object(sketch.id))
            basis=part.place_body_mesh(std::move(basis),body->scope.id);
          const auto point=basis.vertices[0];
          const auto normal=kernel::dimension_unit(kernel::dimension_sub(basis.vertices[3],point));
          const auto* owner=part.find_container(sketch.owner_container_id);
          for(const auto& axis:mesh.axes) {
            if(axis.reference.owner_id!=sketch.owner_container_id &&
               (!owner || axis.reference.owner_id!=owner->feature_id))continue;
            const auto direction=kernel::dimension_unit(axis.direction);
            if(std::abs(kernel::dimension_dot(direction,normal))<1-1e-7)continue;
            const auto delta=kernel::dimension_sub(point,axis.point);
            const auto radial=kernel::dimension_sub(delta,kernel::dimension_scale(direction,kernel::dimension_dot(delta,direction)));
            if(kernel::dimension_dot(radial,radial)>1e-12)continue;
            const auto geometry=frames.find({axis.reference.owner_id,{}});
            if(geometry==frames.end() || !geometry->second.valid)continue;
            kernel::ModelEnvelope frame;frame.origin=axis.point;
            frame.axes={kernel::dimension_unit(kernel::dimension_sub(basis.vertices[1],point)),
                kernel::dimension_unit(kernel::dimension_sub(basis.vertices[2],point)),normal};
            double low=std::numeric_limits<double>::infinity(),high=-low;
            for(auto corner:geometry->second.corners()) {
              const double t=kernel::dimension_dot(kernel::dimension_sub(corner,frame.origin),normal);
              low=std::min(low,t);high=std::max(high,t);
            }
            frame.minimum={-circle.radius,-circle.radius,low};
            frame.maximum={circle.radius,circle.radius,high};frame.valid=true;
            const auto key=std::pair{axis.reference.owner_id,axis.reference.semantic_key};
            auto found=axis_frames.find(key);
            if(found==axis_frames.end() || circle.radius<found->second.maximum.x)axis_frames[key]=frame;
          }
        }
        append(mesh, std::move(packet));
      }
      if (!occurrence.occurrence_ids.empty()) mesh.dimensions.clear();
    };
    const auto assembly_mesh = [&](const assembly::AssemblyDocument &assembly) {
      if (assembly.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      const auto scene=assembly.build_scene();envelope=kernel::model_envelope(scene);frames=scene.annotation_frames;layouts=assembly.dimension_layouts;
      std::erase_if(frames,[](const auto& entry){return !entry.first.second.empty();});
      append(mesh, assembly.construction_viewer_mesh());
      append(mesh, assembly.origin_viewer_mesh());
      frames[{assembly.document_id+":origin",{}}]=envelope;
      // Only dimensions owned by this Assembly; child Parts contribute axes
      // and construction references, never their modeling dimensions.
      for (auto dimension : assembly.build_scene().dimensions)
        if (dimension.reference.owner_id == id && dimension.reference.instance_path.empty())
          mesh.dimensions.push_back(std::move(dimension));
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
    auto extension=path.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (workspace && workspace->open_part(id))
      part_mesh(workspace->open_part(id)->session.document(),
                workspace->authoritative_viewer_mesh(id));
    else if (workspace && workspace->open_assembly(id))
      assembly_mesh(workspace->open_assembly(id)->session.document());
    else if (extension == ".prtz") {
      std::vector<kernel::BodyResult> boundaries;
      auto part = document::PartDocument::load(path, &boundaries);
      Workspace source;
      source.add_part(std::move(part),std::move(boundaries),path);
      if(!source.open_part(id))throw std::runtime_error("Annotation source document identity mismatch");
      part_mesh(source.open_part(id)->session.document(),source.authoritative_viewer_mesh(id));
    } else if (extension == ".asmz")
      assembly_mesh(assembly::AssemblyDocument::load(path));
    else
      throw std::runtime_error("Zdroj anotací není dostupný: " + document::path_to_utf8(path));
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
    frames[{}]=envelope;
    const auto geometry_frames=frames;
    frames=kernel::object_envelopes(mesh,std::move(frames));
    for(const auto& [key,frame]:geometry_frames)if(frame.valid)frames[key]=frame;
    for(const auto& [key,frame]:frames) {
      mesh.vertices.push_back(frame.origin);
      for(auto axis:frame.axes)mesh.vertices.push_back(kernel::dimension_add(frame.origin,axis));
    }
    for(const auto& [key,frame]:axis_frames) {
      mesh.vertices.push_back(frame.origin);
      for(auto axis:frame.axes)mesh.vertices.push_back(kernel::dimension_add(frame.origin,axis));
    }
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
      kernel::BodyResult snapshot;
      snapshot.mesh = std::move(mesh);
      c.calculated_source = std::move(snapshot);
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
    std::size_t frame_index=mesh.dimensions.size();
    std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope> placed_frames;
    for(auto [key,frame]:frames) {
      frame.origin=mesh.vertices.at(frame_index++);
      for(auto& axis:frame.axes)axis=kernel::dimension_sub(mesh.vertices.at(frame_index++),frame.origin);
      placed_frames[{key.first,occurrence.encoded()}]=frame;
    }
    for(auto& [key,frame]:axis_frames) {
      frame.origin=mesh.vertices.at(frame_index++);
      for(auto& axis:frame.axes)axis=kernel::dimension_sub(mesh.vertices.at(frame_index++),frame.origin);
    }
    frames=std::move(placed_frames);
    envelope=frames.at({{},occurrence.encoded()});
    for (std::size_t i = 0; i < mesh.dimensions.size(); ++i) {
      mesh.dimensions[i].label_position = mesh.vertices.at(i);
      mesh.dimensions[i].reference.instance_path = occurrence.encoded();
    }
    for (auto &e : mesh.edges)
      e.reference.instance_path = occurrence.encoded();
    for (auto &a : mesh.axes)
      a.reference.instance_path = occurrence.encoded();
    result.push_back({id, occurrence.encoded(), std::move(mesh.dimensions),
                      std::move(mesh.edges), std::move(mesh.axes),envelope,std::move(layouts),std::move(frames),std::move(axis_frames)});
    stack.erase(id);
  };
  visit(root, root_path, {}, {});
  return result;
}
} // namespace zima::workspace
