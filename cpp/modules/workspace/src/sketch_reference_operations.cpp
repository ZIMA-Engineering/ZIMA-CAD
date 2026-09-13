#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <map>
#include <set>
#include <tuple>
#include <cmath>

namespace zima::workspace {
void populate_external_reference_cache(
    const zima::sketcher::Sketch& sketch,
    zima::sketcher::SketchExternalReference& reference,
    const zima::kernel::ViewerReferenceGeometry& source) {
    const auto matches = [&](const auto& candidate) {
        return candidate.owner_id == reference.source_owner_id &&
            candidate.semantic_key == reference.source_semantic_key &&
            candidate.instance_path == reference.source_instance_path;
    };
    const auto unique=[&](const auto& values) {
        if(std::ranges::count_if(values,[&](const auto& value){return matches(value.reference);})>1)
            throw std::invalid_argument("Persisted source reference is ambiguous");
    };
    if(reference.kind==sketcher::ExternalReferenceKind::Edge)unique(source.edges);
    if(reference.kind==sketcher::ExternalReferenceKind::Point)unique(source.points);
    if(reference.kind==sketcher::ExternalReferenceKind::Axis)unique(source.axes);
    if (reference.kind == zima::sketcher::ExternalReferenceKind::Edge) {
        const auto edge = std::find_if(source.edges.begin(), source.edges.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (edge == source.edges.end()) {
            throw std::runtime_error("Persisted source edge geometry is unavailable");
        }
        reference.exact_spline=sketch.project_external_spline(*edge);
        for (const auto& point : edge->points) {
            const auto local = sketch.local_point(point);
            if (reference.cached_points.empty() || std::hypot(
                    local[0] - reference.cached_points.back()[0],
                    local[1] - reference.cached_points.back()[1]) > 1.0e-9) {
                reference.cached_points.push_back(local);
            }
        }
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Point) {
        const auto point = std::find_if(source.points.begin(), source.points.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (point == source.points.end()) {
            throw std::runtime_error("Persisted source point geometry is unavailable");
        }
        reference.cached_points.push_back(sketch.local_point(point->position));
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Axis) {
        const auto axis = std::find_if(source.axes.begin(), source.axes.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (axis == source.axes.end()) {
            throw std::runtime_error("Persisted source axis geometry is unavailable");
        }
        const auto projected = sketch.project_external_axis(*axis);
        if (!projected) {
            throw std::runtime_error(
                "Source axis cannot be projected into the Sketch plane");
        }
        reference.cached_points = *projected;
        reference.infinite = true;
    } else {
        const auto projected = sketch.project_external_face_plane(source,
            {reference.source_owner_id, reference.source_semantic_key,
             reference.source_instance_path});
        if (projected) {
            reference.cached_points = *projected;
            reference.infinite = true;
        } else if (const auto intersections = sketch.external_face_reference_paths(source,
                       {reference.source_owner_id, reference.source_semantic_key,
                        reference.source_instance_path})) {
            reference.cached_paths = *intersections;
            reference.infinite = false;
        } else {
            throw std::runtime_error(
                "Source face has no unique intersection with the Sketch plane");
        }
    }
    reference.broken = false;
}

namespace {
using Kind=sketcher::ExternalReferenceKind;
using Reference=sketcher::SketchExternalReference;
using Key=std::tuple<Kind,std::string,std::string,std::string>;
// Copy only selected original geometry, not a complete Part or Assembly mesh.
kernel::ViewerReferenceGeometry collect(const Workspace& live,const std::string& doc,
    const sketcher::Sketch& sketch,const std::vector<Reference>& wanted) {
    std::set<Key> keys;
    for(const auto& r:wanted)keys.emplace(r.kind,r.source_owner_id,r.source_semantic_key,r.source_instance_path);
    kernel::ViewerReferenceGeometry result;
    if(keys.empty())return result;
    visit_original_references(live,doc,[&](const auto& source,const ReferenceFrame& frame) {
        try {
            append_original_reference_geometry(result,source,frame,[&](OriginalReferenceKind kind,
                const std::string& owner,const std::string& key,const std::string& path) {
                const auto sketch_kind=kind==OriginalReferenceKind::Face?Kind::Face:kind==OriginalReferenceKind::Edge?Kind::Edge:
                    kind==OriginalReferenceKind::Point?Kind::Point:Kind::Axis;
                return keys.contains({sketch_kind,owner,key,path});
            });
        }catch(const ReferenceQueryError& error){throw SketchOperationError(error.code,error.what());}
        return true;
    });
    if(const auto* part=live.open_part(doc))return part->session.document().sketch_reference_geometry_for(sketch,std::move(result));
    return result;
}
std::optional<std::string> source_document(const Workspace& live,const std::string& doc,
    const sketcher::Sketch& sketch,const Reference& reference,const std::set<std::string>* allowed=nullptr) {
    if(const auto* part=live.open_part(doc)) {
        if(!reference.source_instance_path.empty() || !(allowed?allowed->contains(reference.source_owner_id):sketch_external_reference_source_owners(part->session.document(),sketch.id).contains(reference.source_owner_id)))return std::nullopt;
        return doc;
    }
    if(live.open_assembly(doc) && !reference.source_instance_path.empty()) {
        const auto address=live.resolve_occurrence(doc,assembly::InstancePath::decode(reference.source_instance_path));
        if(address)return address->source_document_id;
    }
    return std::nullopt;
}
}
sketcher::SketchExternalReference prepare_sketch_external_reference(const Workspace& live,
    const std::string& doc,const sketcher::Sketch& sketch,Kind kind,const std::string& owner,
    const std::string& key,const std::string& path) {
    auto reference=sketcher::Sketch::create_external_reference(kind);
    reference.source_owner_id=owner;reference.source_semantic_key=key;reference.source_instance_path=path;
    const auto source=source_document(live,doc,sketch,reference);
    if(!source)throw SketchOperationError("invalid_reference_source","The reference must identify an earlier Part object or an exact Assembly occurrence.");
    reference.source_document_id=*source;
    const auto geometry=collect(live,doc,sketch,{reference});
    populate_external_reference_cache(sketch,reference,geometry);
    return reference;
}
bool refresh_sketch_reference_snapshot(const Workspace& live,const std::string& doc,sketcher::Sketch& sketch) {
    std::set<std::string> documents;std::vector<Reference> wanted;
    std::optional<std::set<std::string>> allowed;
    if(const auto* part=live.open_part(doc))allowed=sketch_external_reference_source_owners(part->session.document(),sketch.id);
    for(const auto& reference:sketch.external_references) {
        if(!reference.context_assembly_document_id.empty())throw SketchOperationError("context_reference","Refresh in-context Sketch dependencies with explicit document Regenerate.");
        documents.insert(reference.source_document_id);
        const auto source=source_document(live,doc,sketch,reference,allowed?&*allowed:nullptr);
        if(source && *source==reference.source_document_id)wanted.push_back(reference);
    }
    const auto geometry=collect(live,doc,sketch,wanted);bool changed=false;
    for(const auto& source:documents)changed=sketch.refresh_external_references(source,geometry)||changed;
    return changed;
}
}
