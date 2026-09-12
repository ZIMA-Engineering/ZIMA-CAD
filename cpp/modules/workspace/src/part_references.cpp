#include <zima/workspace/model_calculation.hpp>
#include <zima/document/feature_sketches.hpp>
#include <algorithm>
#include <cstdint>
#include <ranges>

namespace zima::workspace {

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
    const std::string& sketch_id) {
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
        if (!target) return owners;
        const auto add_construction = [&](const auto& self, const auto& object) -> void {
            owners.insert(object.id); owners.insert(object.entity_id);
            owners.insert(object.container_origin.id);
            for (const auto& point : object.curve_points) self(self, point);
        };
        for (const auto& body : document.body_history.bodies()) {
            for (const auto& entry : body.entries) {
                if (entry.id == consumer) return owners;
                owners.insert(entry.id);
                if (const auto* object = document.find_construction(entry.id))
                    add_construction(add_construction, *object);
                if (const auto* feature = document.find_container(entry.id)) {
                    owners.insert(feature->container_origin.id);
                    if (feature->feature_kind == zima::document::FeatureKind::Sweep3D)
                        add_construction(add_construction, feature->sweep3d.path);
                }
                for (const auto& source_sketch : document.sketches)
                    if (source_sketch.owner_container_id == entry.id) owners.insert(source_sketch.id);
            }
            if (body.scope.id == target->scope.id) break;
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
    append_reference_geometry(
        geometry, document.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry, document.body_origin_reference_geometry());
    append_reference_geometry(
        geometry, document.construction_viewer_mesh().original_references);
    return geometry;
}

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    const auto source = sketch_external_reference_source_geometry(
        document, calculated_boundaries);
    bool changed = false;
    const auto refresh=[&](zima::sketcher::Sketch& sketch) {
        const auto allowed_owners = sketch_external_reference_source_owners(
            document, sketch.id);
        zima::kernel::ViewerReferenceGeometry allowed_source;
        for (const auto& edge : source.edges) {
            if (allowed_owners.contains(edge.reference.owner_id)) {
                allowed_source.edges.push_back(edge);
            }
        }
        for (const auto& point : source.points) {
            if (allowed_owners.contains(point.reference.owner_id)) {
                allowed_source.points.push_back(point);
            }
        }
        for (const auto& axis : source.axes) {
            if (allowed_owners.contains(axis.reference.owner_id)) {
                allowed_source.axes.push_back(axis);
            }
        }
        allowed_source.vertices = source.vertices;
        for (std::size_t triangle = 0;
             triangle < source.triangle_references.size(); ++triangle) {
            const auto& face = source.triangle_references[triangle];
            if (!allowed_owners.contains(face.owner_id)) continue;
            allowed_source.triangle_references.push_back(face);
            allowed_source.triangles.insert(allowed_source.triangles.end(), {
                source.triangles[triangle * 3],
                source.triangles[triangle * 3 + 1],
                source.triangles[triangle * 3 + 2]});
        }
        if (sketch.refresh_external_references(
                document.document_id, document.sketch_reference_geometry_for(sketch, std::move(allowed_source)))) {
            changed = true;
        }
    };
    for(auto& sketch:document.sketches)refresh(sketch);
    for(auto& container:document.history)if(container.feature_kind==zima::document::FeatureKind::Sweep2D)
        for(auto& data:container.sweep2d.sketches()){auto sketch=zima::sketcher::Sketch::from_serialized(data);refresh(sketch);data=sketch.serialized();}
    return changed;
}

bool prune_missing_drill_point_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& boundaries) {
    bool changed = false;
    const auto operations = document.kernel_operations();
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
    const auto source = document.build_scene().original_references;
    bool changed = false;
    for (auto& sketch : document.sketches) {
        std::set<std::string> source_documents;
        for (const auto& reference : sketch.external_references) {
            if (!reference.source_document_id.empty()) {
                source_documents.insert(reference.source_document_id);
            }
        }
        for (const auto& source_document_id : source_documents) {
            changed = sketch.refresh_external_references(
                source_document_id, source) || changed;
        }
    }
    return changed;
}

} // namespace zima::workspace
