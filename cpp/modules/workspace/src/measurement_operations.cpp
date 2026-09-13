#include <zima/workspace/measurement_operations.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <map>
#include <tuple>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
namespace zima::workspace {
kernel::ViewerMesh measurement_scene(const Workspace& live,const std::string& id) {
    auto scene=live.authoritative_viewer_mesh(id);
    const auto origin=live.open_part(id)?live.open_part(id)->session.document().origin_viewer_mesh():
        live.open_assembly(id)->session.document().origin_viewer_mesh();
    // The model scene already contains its calculated geometry and Sketches.
    // Add the root datum only when it is absent, independently of eye state.
    std::map<std::tuple<OriginalReferenceKind,std::string,std::string,std::string>,bool> decisions;
    append_original_reference_geometry(scene.original_references,origin.original_references,{},
        [&](OriginalReferenceKind kind,const std::string& owner,const std::string& key,const std::string& path) {
            const auto identity=std::tuple{kind,owner,key,path};
            if(const auto found=decisions.find(identity);found!=decisions.end())return found->second;
            const auto matches=[&](const auto& ref){return ref.owner_id==owner&&ref.semantic_key==key&&ref.instance_path==path;};
            const auto& refs=scene.original_references;
            const bool missing=[&]{switch(kind) {
            case OriginalReferenceKind::Face:return std::ranges::none_of(refs.triangle_references,matches);
            case OriginalReferenceKind::Edge:return std::ranges::none_of(refs.edges,[&](const auto& item){return matches(item.reference);});
            case OriginalReferenceKind::Point:return std::ranges::none_of(refs.points,[&](const auto& item){return matches(item.reference);});
            case OriginalReferenceKind::Axis:return std::ranges::none_of(refs.axes,[&](const auto& item){return matches(item.reference);});
            }
            return false;}();
            decisions.emplace(identity,missing);return missing;
        });
    return scene;
}
std::optional<measurement::MeasurementGeometry> resolve_measurement(
    const Workspace& live,const std::string& id,const kernel::MeasurementReference& reference,const kernel::ViewerMesh& scene) {
    auto geometry=measurement::measure_entity(scene,reference);
    if(reference.kind==kernel::MeasurementKind::Object && reference.owner_id.empty()&&!reference.instance_path.empty()){
        // Resolve metadata for this exact occurrence, without calculating its source.
        const auto address=live.resolve_occurrence(id,assembly::InstancePath::decode(reference.instance_path));
        if(geometry&&address)if(const auto* owner=live.open_assembly(address->owner_assembly_document_id))
            if(const auto* item=owner->session.document().find_occurrence(address->occurrence_id)){
                geometry->values.volume=kernel::MeasurementValue{std::abs(item->calculated_source->volume),false};
                geometry->values.area=kernel::MeasurementValue{std::abs(item->calculated_source->surface_area),false};
                if(const auto mass=assembly::occurrence_mass_kg(*item))geometry->values.mass=kernel::MeasurementValue{*mass,false};
            }
    }
    if(!geometry)return {};
    if(const auto* part=live.open_part(id)){
        if(reference.kind==kernel::MeasurementKind::Object){
            // A single-owner snapshot is the selected source solid. Never use
            // a later multi-feature total as the volume of an earlier object.
            for(const auto& result:part->session.calculated_boundaries()){
                const auto& faces=result.mesh.original_references.triangle_references;
                if(!faces.empty()&&std::ranges::all_of(faces,[&](const auto& ref){return ref.owner_id==reference.owner_id;})){
                    geometry->values.volume=kernel::MeasurementValue{std::abs(result.volume),false};
                    geometry->values.area=kernel::MeasurementValue{std::abs(result.surface_area),false};break;
                }
            }
        }
        if(geometry->values.volume)if(const auto density=document::material_density_kg_mm3(part->session.document()))
            geometry->values.mass=kernel::MeasurementValue{geometry->values.volume->value * *density,geometry->values.volume->approximate};
    }
    return geometry;
}
}
