#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/loft.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>

namespace zima::workspace {

namespace {
zima::kernel::ViewerReferenceGeometry reference_geometry_for_owners(
    const zima::kernel::ViewerReferenceGeometry& source,
    const std::set<std::string>& owners) {
    zima::kernel::ViewerReferenceGeometry selected;
    for(const auto& edge:source.edges)
        if(owners.contains(edge.reference.owner_id))selected.edges.push_back(edge);
    for(const auto& point:source.points)
        if(owners.contains(point.reference.owner_id))selected.points.push_back(point);
    for(const auto& axis:source.axes)
        if(owners.contains(axis.reference.owner_id))selected.axes.push_back(axis);
    const auto selected_triangle_count=static_cast<std::size_t>(std::ranges::count_if(
        source.triangle_references,[&](const auto& reference) {
            return owners.contains(reference.owner_id);
        }));
    selected.triangle_references.reserve(selected_triangle_count);
    selected.triangles.reserve(selected_triangle_count*3);
    selected.vertices.reserve(selected_triangle_count*3);
    for(std::size_t triangle=0;triangle<source.triangle_references.size();++triangle) {
        const auto& reference=source.triangle_references[triangle];
        if(!owners.contains(reference.owner_id))continue;
        const auto source_offset=triangle*3;
        if(source_offset+2>=source.triangles.size())
            throw std::runtime_error("Reference face triangle packet is incomplete");
        const auto selected_offset=static_cast<std::uint32_t>(selected.vertices.size());
        for(std::size_t corner=0;corner<3;++corner) {
            const auto vertex=source.triangles[source_offset+corner];
            if(vertex>=source.vertices.size())
                throw std::runtime_error("Reference face triangle index is invalid");
            selected.vertices.push_back(source.vertices[vertex]);
            selected.triangles.push_back(selected_offset+static_cast<std::uint32_t>(corner));
        }
        selected.triangle_references.push_back(reference);
    }
    return selected;
}
}

void append_reference_geometry(
    zima::kernel::ViewerReferenceGeometry& target,
    zima::kernel::ViewerReferenceGeometry source) {
    const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(
        target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) {
        target.triangles.push_back(index + vertex_offset);
    }
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id, const std::string& draft_body_id, bool draft_section) {
    std::size_t first_consumer = document.history.size();
    const auto sketch = std::ranges::find_if(document.sketches,
        [&](const auto& value) { return value.id == sketch_id; });
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        const bool extrusion_consumer =
            container.feature_kind == zima::document::FeatureKind::Extrusion &&
            container.extrusion.sketch_id == sketch_id;
        const bool revolution_consumer =
            container.feature_kind == zima::document::FeatureKind::Revolution &&
            container.revolution.sketch_id == sketch_id;
        bool embedded_consumer=false;
        zima::document::visit_feature_sketches(container,[&](const auto& data,std::size_t) {
            if(!embedded_consumer)embedded_consumer=zima::sketcher::Sketch::from_serialized(data).id==sketch_id;
        });
        if (extrusion_consumer || revolution_consumer || embedded_consumer ||
            (sketch != document.sketches.end() && sketch->owner_container_id == container.id)) {
            first_consumer = std::min(first_consumer, index);
        }
    }
    std::set<std::string> owners;
    if (!document.body_history.bodies().empty()) {
        const auto consumer = first_consumer < document.history.size()
            ? document.history[first_consumer].id : sketch_id;
        const auto* target = document.body_history.owner(consumer);
        // A new dialog-owned Sketch has no persistent history entry yet.
        // Its caller supplies the owning Body explicitly; the insertion
        // cursor provides the same preceding-source boundary as its commit.
        const bool draft = !target && first_consumer == document.history.size() &&
            !draft_body_id.empty();
        if (draft) target = document.body_history.find(draft_body_id);
        const bool section=(!target && first_consumer==document.history.size() && draft_section) ||
            std::ranges::any_of(document.sections,[&](const auto& value){return value.sketch.id==sketch_id;});
        if (!target && !section) return owners;
        const auto add_construction = [&](const auto& self, const auto& object) -> void {
            owners.insert(object.id); owners.insert(object.entity_id);
            owners.insert(object.container_origin.id);
            for (const auto& point : object.curve_points) self(self, point);
        };
        for (const auto& body : document.body_history.bodies()) {
            for (std::size_t index = 0; index < body.entries.size(); ++index) {
                if (draft && target && body.scope.id == target->scope.id &&
                    index >= body.cursor) return owners;
                const auto& entry = body.entries[index];
                if (entry.id == consumer) return owners;
                owners.insert(entry.id);
                if (const auto* object = document.find_construction(entry.id))
                    add_construction(add_construction, *object);
                if (const auto* feature = document.find_container(entry.id)) {
                    owners.insert(feature->container_origin.id);
                    if (feature->feature_kind == zima::document::FeatureKind::Sweep3D)
                        add_construction(add_construction, feature->sweep3d.path);
                    if(feature->feature_kind==zima::document::FeatureKind::GeneralSurface||feature->feature_kind==zima::document::FeatureKind::Loft)
                        for(const auto& boundary:feature->feature_kind==zima::document::FeatureKind::Loft?feature->loft.sketches:feature->general_surface.boundaries)
                            if(boundary.curve)add_construction(add_construction,*boundary.curve);
                            else {
                                owners.insert(zima::sketcher::Sketch::from_serialized(boundary.sketch_serialized).id);
                                const auto& owned=*boundary.sketch_feature;
                                owners.insert(owned.id);owners.insert(owned.feature_id);owners.insert(owned.container_origin.id);
                                for(const auto& child:owned.container_origin.children)owners.insert(child.id);
                            }
                }
                for (const auto& source_sketch : document.sketches)
                    if (source_sketch.owner_container_id == entry.id) owners.insert(source_sketch.id);
            }
            if (target && body.scope.id == target->scope.id) break;
        }
        return owners;
    }
    for (std::size_t index = 0; index < first_consumer; ++index) {
        owners.insert(document.history[index].id);
    }
    for (const auto& construction : document.constructions) {
        owners.insert(construction.id);
    }
    return owners;
}

zima::kernel::ViewerReferenceGeometry sketch_external_reference_source_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
    append_reference_geometry(source,
        document.construction_viewer_mesh().original_references);
    // Standalone Sketches do not necessarily contribute a calculated Body.
    // Their authored points and curves still remain valid preceding sources.
    const auto sketches = document.sketch_placement_reference_geometry();
    const auto append_missing = [](auto& target, const auto& values) {
        std::set<std::tuple<std::string,std::string,std::string>> identities;
        const auto identity=[](const auto& value) {
            return std::tuple{value.reference.owner_id,value.reference.semantic_key,value.reference.instance_path};
        };
        for(const auto& value:target)identities.insert(identity(value));
        for (const auto& value : values)
            if (identities.insert(identity(value)).second) target.push_back(value);
    };
    append_missing(source.points, sketches.points);
    append_missing(source.edges, sketches.edges);
    append_missing(source.axes, sketches.axes);
    for(const auto& feature:document.history)if((feature.feature_kind==zima::document::FeatureKind::GeneralSurface||feature.feature_kind==zima::document::FeatureKind::Loft)&&!feature.suppressed) {
        auto mesh=feature.feature_kind==document::FeatureKind::Loft?document::general_surface_definition_mesh(document::loft_sketch_editor_carrier(feature)):document::general_surface_definition_mesh(feature);
        if(const auto* body=document.body_owner_for_object(feature.id))mesh=document.place_body_mesh(std::move(mesh),body->scope.id);
        append_reference_geometry(source,std::move(mesh.original_references));
    }
    return source;
}

zima::kernel::ViewerReferenceGeometry construction_reference_source_geometry(
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
    return source;
}

zima::kernel::ViewerReferenceGeometry part_construction_dimension_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    auto geometry =
        construction_reference_source_geometry(calculated_boundaries);
    append_reference_geometry(geometry, document.sketch_placement_reference_geometry());
    append_reference_geometry(
        geometry, document.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry, document.body_origin_reference_geometry());
    append_reference_geometry(
        geometry, document.construction_viewer_mesh().original_references);
    return geometry;
}

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries,const kernel::OcctKernel* explicit_kernel) {
    bool has_external_references=false;
    visit_document_sketches(document,[&](const auto& sketch) {
        has_external_references=!sketch.external_references.empty();
        return !has_external_references;
    });
    if(!has_external_references) {
        // Preserve retirement of stale errors, but do not copy a complete body
        // reference packet when no native or embedded Sketch can consume it.
        const bool changed=!document.reference_errors.empty();
        document.reference_errors.clear();return changed;
    }
    const bool profile_enabled=std::getenv("ZIMA_CPP_PROFILE_COMMAND")!=nullptr;
    auto profile_start=profile_enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto profile=[&](const std::string& sketch,const char* phase) {
        if(!profile_enabled)return;
        const auto now=std::chrono::steady_clock::now();
        std::fprintf(stderr,"Sketch reference refresh sketch=%s %s: %.3f ms\n",sketch.c_str(),phase,
            std::chrono::duration<double,std::milli>(now-profile_start).count());
        profile_start=now;
    };
    const auto source = sketch_external_reference_source_geometry(
        document, calculated_boundaries);
    profile({},"source geometry");
    // The input boundaries and resolved placement frames are fixed for this
    // settling pass. Share one private query context across its Sketches.
    std::optional<document::DocumentSession> query_context;
    const auto refresh=[&](zima::sketcher::Sketch& sketch) {
        const auto allowed_owners = sketch_external_reference_source_owners(
            document, sketch.id);
        profile(sketch.id,"allowed owners");
        std::set<std::string> referenced_owners;
        for(const auto& reference:sketch.external_references)
            if(!reference.body_edge&&allowed_owners.contains(reference.source_owner_id))
                referenced_owners.insert(reference.source_owner_id);
        auto allowed_source=reference_geometry_for_owners(source,referenced_owners);
        profile(sketch.id,"filtered geometry");
        kernel::ViewerReferenceGeometry body;
        if(std::ranges::any_of(sketch.external_references,[](const auto& r){return r.body_edge;})) {
            if(!query_context)query_context.emplace(document,calculated_boundaries);
            body=part_sketch_body_reference_geometry(*query_context,sketch);
        }
        profile(sketch.id,"Body geometry");
        auto geometry=document.sketch_reference_geometry_for(sketch,std::move(allowed_source));
        // Owned Sketch frames are stored relative to their owning feature.
        // Body reference packets have already been converted to Body-local space.
        for(const auto& parent:document.history)if((parent.feature_kind==document::FeatureKind::GeneralSurface||parent.feature_kind==document::FeatureKind::Loft))
            for(const auto& boundary:parent.feature_kind==document::FeatureKind::Loft?parent.loft.sketches:parent.general_surface.boundaries)
                if(boundary.sketch_feature&&boundary.sketch_feature->id==sketch.owner_container_id) {
                    geometry=document::general_surface_local_reference_geometry(parent,std::move(geometry));
                    body=document::general_surface_local_reference_geometry(parent,std::move(body));
                }
        profile(sketch.id,"local geometry");
        std::vector<sketcher::SketchExternalReference> intersections;
        if(explicit_kernel && std::ranges::any_of(sketch.external_references,[](const auto& r){return sketcher::is_intersection_reference(r);})) {
            if(!query_context)query_context.emplace(document,calculated_boundaries);
            intersections=calculate_part_sketch_intersections(*explicit_kernel,*query_context,sketch);
        }
        profile(sketch.id,"intersections");
        const auto changed=sketch.refresh_external_references(
            document.document_id,geometry,false,&body,false,explicit_kernel?&intersections:nullptr);
        profile(sketch.id,"projection");
        return changed;
    };
    const auto before=document.reference_errors;
    std::map<std::string,std::string> errors;
    const bool changed=update_document_sketches(document,[&](auto& sketch) {
        if(sketch.external_references.empty())return false;
        // Refresh is transactional per Sketch. A failed later profile must not
        // invalidate the successfully calculated predecessors that drive it.
        try {
            return refresh(sketch);
        } catch(const std::exception& error) {
            auto owner=sketch.owner_container_id;
            for(const auto& parent:document.history)if((parent.feature_kind==document::FeatureKind::GeneralSurface||parent.feature_kind==document::FeatureKind::Loft))
                for(const auto& boundary:parent.feature_kind==document::FeatureKind::Loft?parent.loft.sketches:parent.general_surface.boundaries)
                    if(boundary.sketch_feature&&boundary.sketch_feature->id==owner)owner=parent.id;
            if(owner.empty()||!document.find_container(owner))throw;
            errors[owner]=sketch.name+": "+error.what();
            return false;
        }
    });
    document.reference_errors=std::move(errors);
    return changed||document.reference_errors!=before;
}

bool prune_missing_drill_point_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& boundaries) {
    if(std::ranges::none_of(document.history,[](const auto& feature) {
            return feature.feature_kind==zima::document::FeatureKind::DrillPoint;
        }))return false;
    bool changed = false;
    const auto operations = document.kernel_operations(false,true);
    for (std::size_t index = 0; index < operations.size(); ++index) {
        auto* container = document.find_container(operations[index].owner_id);
        if (container == nullptr || container->feature_kind !=
                zima::document::FeatureKind::DrillPoint) continue;
        const auto* available = index == 0 || index - 1 >= boundaries.size()
            ? nullptr
            : &boundaries[index - 1].mesh.original_references
                .triangle_references;
        const auto before = container->drill_point.bottom_faces.size();
        std::erase_if(container->drill_point.bottom_faces,
            [&](const auto& face) {
                return available == nullptr || std::ranges::none_of(
                    *available, [&](const auto& candidate) {
                        return candidate.owner_id == face.owner_id &&
                            candidate.semantic_key == face.semantic_key;
                    });
            });
        changed = changed ||
            container->drill_point.bottom_faces.size() != before;
    }
    return changed;
}

bool refresh_assembly_sketch_external_references(
    zima::assembly::AssemblyDocument& document) {
    const auto scene = document.build_scene();
    const auto& source=scene.original_references;
    return update_document_sketches(document,[&](auto& sketch) {
        kernel::ViewerReferenceGeometry body;
        if(std::ranges::any_of(sketch.external_references,[](const auto& reference){return reference.body_edge;}))
            body=assembly_sketch_body_reference_geometry(document,sketch);
        bool changed=false;
        std::set<std::string> source_documents;
        for(const auto& reference:sketch.external_references)
            if(!reference.source_document_id.empty())source_documents.insert(reference.source_document_id);
        for(const auto& source_document_id:source_documents)
            changed=sketch.refresh_external_references(source_document_id,source,false,&body)||changed;
        return changed;
    });
}

} // namespace zima::workspace
