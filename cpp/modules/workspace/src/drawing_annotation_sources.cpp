#include <zima/document/feature_parameter_dimensions.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sheet_form_definition.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <algorithm>
#include <cctype>
#include <zima/document/file_path.hpp>
#include <functional>
#include <stdexcept>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
namespace {
symbols::Placement form_symbol(const document::PartDocument& part,
        const document::HistoryContainer& feature,const kernel::SheetMaterialDefinition& material) {
  using namespace kernel::sheet_material;
  const auto source=document::stored_sheet_form_definition(feature.sheet_form);
  const auto original=std::ranges::find(source.part.sketches,source.symbol_sketch,&sketcher::Sketch::id);
  if(original==source.part.sketches.end())throw std::invalid_argument("Invalid FORM definition.");
  // Preserve the independent authored Sketch. The drawing adapter owns a
  // standalone local XY copy of its evaluated curves; external supports and
  // dimensions are editing context, rather than manufacturing symbol strokes.
  auto sketch=original->evaluated_profile_sketch();
  sketch.owner_container_id.clear();sketch.plane_reference_owner_id.clear();
  sketch.external_references.clear();sketch.constraints.clear();sketch.dimensions.clear();
  sketch.plane=sketcher::SketchPlane::XY;sketch.plane_offset=0;sketch.refresh_default_frame();
  symbols::Definition definition;definition.id="form:symbol-definition:"+feature.id;
  definition.name=feature.sheet_form.source_name;definition.default_variant="default";
  definition.variants[definition.default_variant].sketches={sketch.id};definition.sketches={std::move(sketch)};
  symbols::Placement result;result.symbol.id="form:symbol:"+feature.id;
  result.symbol.definition=definition.serialized();result.symbol.variant=definition.default_variant;
  const auto request=document::sheet_form_request(source,{}, {},{0,1,0},{1,0,0},feature.sheet_form.thickness);
  kernel::ViewerMesh basis;const auto origin=original->world_point(0,0);
  basis.vertices={origin,add(origin,original->x_axis()),add(origin,original->y_axis())};
  basis=source.part.place_body_mesh(std::move(basis),source.bodies[3]);
  const auto source_z=cross(request.source_x,request.source_normal);
  const auto destination_z=cross(material.along,material.radial);
  const auto vector=[&](kernel::Vec3 p) {return add(mul(material.along,dot(p,request.source_x)),
      add(mul(material.radial,dot(p,request.source_normal)),mul(destination_z,dot(p,source_z))));};
  result.frame.origin=add(material.origin,vector(sub(basis.vertices[0],request.source_origin)));
  result.frame.x=vector(sub(basis.vertices[1],basis.vertices[0]));
  result.frame.y=vector(sub(basis.vertices[2],basis.vertices[0]));
  basis.vertices={result.frame.origin,add(result.frame.origin,result.frame.x),add(result.frame.origin,result.frame.y)};
  if(const auto* body=part.body_owner_for_object(feature.id))basis=part.place_body_mesh(std::move(basis),body->scope.id);
  result.frame={basis.vertices[0],sub(basis.vertices[1],basis.vertices[0]),sub(basis.vertices[2],basis.vertices[0])};
  result.unresolved=!feature.placement.reference_valid;result.validate();return result;
}
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
  for (auto &edge : mesh.edges) {
    for (auto &p : edge.points)
      p = point(p);
    if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=point(p);
  }
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
    if(d.measurement_direction)d.measurement_direction=vector(*d.measurement_direction);
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
    std::map<std::string,drawing::ThreadDesignation> threads;
    std::vector<symbols::Placement> native_symbols;
    const auto part_mesh = [&](const document::PartDocument &part,
                               const kernel::ViewerMesh &calculated,
                               const std::map<std::string,std::string>& calculation_errors) {
      if (part.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      envelope=kernel::model_envelope(calculated);frames=document::part_annotation_envelopes(part,calculated);layouts=part.dimension_layouts;
      native_symbols=part.symbol_annotations;
      if(std::ranges::any_of(part.history,[](const auto& feature){return !feature.suppressed&&feature.feature_kind==document::FeatureKind::SheetForm;})) {
        // Reuse native material-state mathematics. This reads authored data;
        // it neither loads source files nor asks OCCT to reconstruct geometry.
        std::map<std::string,std::vector<kernel::HistoryOperation>> bodies;
        for(auto operation:part.kernel_operations())bodies[operation.body.id].push_back(std::move(operation));
        for(const auto& [body_id,operations]:bodies) {
          const auto* body=part.body_history.find(body_id);
          if(body&&(body->suppressed||!body->visible))continue;
          const auto state=kernel::sheet_material::regions_before(operations,operations.size());
          for(const auto& region:state.regions)if(region.kind==kernel::SheetMaterialDefinition::Kind::Form) {
            const auto* feature=part.find_container(region.owner_id);
            if(feature&&!feature->suppressed) {
              auto symbol=form_symbol(part,*feature,region);
              symbol.unresolved=symbol.unresolved||calculation_errors.contains(feature->id);
              native_symbols.push_back(std::move(symbol));
            }
          }
        }
      }
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
      for(const auto& axis:calculated.original_references.axes) {
        const auto* owner=part.find_container(axis.reference.owner_id);
        if(axis.reference.semantic_key=="axis:primary"&&owner&&owner->revolution.sheet_metal)continue;
        mesh.axes.push_back(axis);
      }
      mesh.axes.insert(mesh.axes.end(), calculated.axes.begin(),
                       calculated.axes.end());
      // Curved rotation/Sweep centerlines are native edges, rather than axes.
      // Offer the current displayed material state, including restored paths.
      for(const auto& edge:calculated.edges) {
        std::string storage;
        if(kernel::solid_state_source_key(edge.reference.semantic_key,storage).starts_with("centerline:from:"))
          mesh.edges.push_back(edge);
      }
      // Historical flat axes remain valid original references, but only the
      // current material state offers bend lines in this Drawing source.
      std::erase_if(mesh.axes,[&](const auto& axis) {
        return kernel::sheet_material::is_bend_line(axis.reference)&&
            std::ranges::none_of(calculated.axes,[&](const auto& active){return active.reference==axis.reference;});
      });
      for(const auto& axis:calculated.axes)if(kernel::sheet_material::is_bend_line(axis.reference)) {
        using namespace kernel::sheet_material;
        kernel::ModelEnvelope frame;frame.origin=axis.point;
        const auto x=unit(axis.direction),y=unit(cross(x,std::abs(x.z)<.9?kernel::Vec3{0,0,1}:kernel::Vec3{0,1,0}));
        frame.axes={x,y,cross(x,y)};frame.minimum={-axis.display_length/2,0,0};frame.maximum={axis.display_length/2,0,0};frame.valid=true;
        axis_frames[{axis.reference.owner_id,axis.reference.semantic_key}]=frame;
      }
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
      for(const auto& feature:part.history) if(!feature.suppressed) {
        if(feature.feature_kind==document::FeatureKind::Thread&&feature.thread.enabled)
          threads[feature.id]={feature.thread.designation,feature.thread.nominal_diameter};
        if(feature.feature_kind==document::FeatureKind::ShaftThread)
          threads[feature.id]={feature.shaft_thread.designation,feature.shaft_thread.nominal_diameter};
        auto packet=document::primitive_parameter_dimensions(feature);
        if(feature.feature_kind==document::FeatureKind::Holes) {
          const auto sketch=std::ranges::find(part.sketches,feature.holes.sketch_id,&sketcher::Sketch::id);
          if(sketch!=part.sketches.end())packet=document::holes_preview(feature,*sketch);
        }
        if(const auto* body=part.body_owner_for_object(feature.id))
          packet=part.place_body_mesh(std::move(packet),body->scope.id);
        append(mesh,std::move(packet));
      }
      for(const auto& feature:part.history)if(!feature.suppressed) {
        document::visit_feature_sketches(feature,[&](const auto& data,std::size_t stage) {
          // The circular path dimension remains an authored radius reference
          // even when the two physical side edges are width-transition curves.
          if(feature.feature_kind==document::FeatureKind::Bend&&stage==0&&feature.bend.angle_degrees==0)return;
          const auto sketch=sketcher::Sketch::from_serialized(data);
          auto packet=sketch.viewer_mesh();
          if(const auto* body=part.body_owner_for_object(feature.id))packet=part.place_body_mesh(std::move(packet),body->scope.id);
          layouts.insert(layouts.begin(),sketch.dimension_layouts.begin(),sketch.dimension_layouts.end());
          frames=kernel::object_envelopes(packet,std::move(frames));
          append(mesh,std::move(packet));
        });
      }
      if (!occurrence.occurrence_ids.empty()) mesh.dimensions.clear();
    };
    const auto assembly_mesh = [&](const assembly::AssemblyDocument &assembly) {
      if (assembly.document_id != id)
        throw std::runtime_error(
            "Annotation source document identity mismatch");
      const auto scene=assembly.build_scene();envelope=kernel::model_envelope(scene);frames=scene.annotation_frames;layouts=assembly.dimension_layouts;
      native_symbols=assembly.symbol_annotations;
      std::erase_if(frames,[](const auto& entry){return !entry.first.second.empty();});
      append(mesh, assembly.construction_viewer_mesh());
      append(mesh, assembly.origin_viewer_mesh());
      frames[{assembly.document_id+":origin",{}}]=envelope;
      // Only dimensions owned by this Assembly; child Parts contribute axes
      // and construction references, never their modeling dimensions.
      for (auto dimension : scene.dimensions)
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
        if (assembly::is_skeleton(component) || suppressed.contains(component.occurrence_id) || !component.visible)
          continue;
        expand(component, occurrence.child(component.occurrence_id), placements,
               {});
      }
    };
    auto extension=path.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (workspace && workspace->open_part(id))
      part_mesh(workspace->open_part(id)->session.document(),
                workspace->authoritative_viewer_mesh(id),
                workspace->open_part(id)->session.calculated_boundaries().empty()?std::map<std::string,std::string>{}:
                    workspace->open_part(id)->session.calculated_boundaries().back().calculation_errors);
    else if (workspace && workspace->open_assembly(id))
      assembly_mesh(workspace->open_assembly(id)->session.document());
    else if (extension == ".prtz") {
      std::vector<kernel::BodyResult> boundaries;
      auto part = read_family_part(workspace,path,id,boundaries);
      Workspace source;
      source.add_part(std::move(part),std::move(boundaries),path);
      if(!source.open_part(id))throw std::runtime_error("Annotation source document identity mismatch");
      part_mesh(source.open_part(id)->session.document(),source.authoritative_viewer_mesh(id),
                source.open_part(id)->session.calculated_boundaries().empty()?std::map<std::string,std::string>{}:
                    source.open_part(id)->session.calculated_boundaries().back().calculation_errors);
    } else if (extension == ".asmz")
      assembly_mesh(read_family_assembly(workspace,path,id));
    else
      throw std::runtime_error("Zdroj anotací není dostupný: " + document::path_to_utf8(path));
    std::erase_if(mesh.dimensions,
                  [](const auto &d) { return !d.reference.valid(); });
    std::erase_if(mesh.edges, [](const auto &e) {
      return !e.reference.valid() || !e.construction;
    });
    std::erase_if(mesh.axes,
                  [](const auto &a) { return !a.reference.valid() || drawing::non_drawing_axis(a.reference.semantic_key); });
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
    // A Sketch centerline used as a Revolution axis is present twice in the
    // persisted source packet: once as its authored construction segment and
    // once as the calculated axis.  Both deliberately keep the same ZIMA
    // reference because they are the same object.  Drawing annotations must
    // expose that object once; the axis carries the useful finite extent and
    // is therefore the canonical presentation.
    const auto axis_references = [&] {
      std::set<std::pair<std::string, std::string>> result;
      for (const auto &axis : mesh.axes)
        result.emplace(axis.reference.owner_id,
                       axis.reference.semantic_key);
      return result;
    }();
    std::erase_if(mesh.edges, [&](const auto &edge) {
      return axis_references.contains(
          {edge.reference.owner_id, edge.reference.semantic_key});
    });
    // Consume the existing Assembly display transformation. Extra vertices
    // carry optional text anchors through exactly the same placement, without
    // solving.
    std::erase_if(mesh.edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("symbol:");});
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
    for(const auto& symbol:native_symbols) {
      mesh.vertices.push_back(symbol.frame.origin);
      mesh.vertices.push_back(kernel::dimension_add(symbol.frame.origin,symbol.frame.x));
      mesh.vertices.push_back(kernel::dimension_add(symbol.frame.origin,symbol.frame.y));
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
    for(auto& symbol:native_symbols) {
      symbol.frame.origin=mesh.vertices.at(frame_index++);
      symbol.frame.x=kernel::dimension_sub(mesh.vertices.at(frame_index++),symbol.frame.origin);
      symbol.frame.y=kernel::dimension_sub(mesh.vertices.at(frame_index++),symbol.frame.origin);
      symbol.frame.validate();
    }
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
                      std::move(mesh.edges), std::move(mesh.axes),envelope,std::move(layouts),std::move(frames),std::move(axis_frames),std::move(threads),std::move(native_symbols)});
    stack.erase(id);
  };
  visit(root, root_path, {}, {});
  return result;
}
} // namespace zima::workspace
