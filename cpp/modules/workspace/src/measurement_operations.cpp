#include <zima/workspace/measurement_operations.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/assembly_scene.hpp>
#include <map>
#include <tuple>
#include <zima/document/physical_properties.hpp>
#include <zima/document/body_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
namespace zima::workspace {
namespace {
const assembly::AssemblyDocument* measurement_owner(const Workspace& live,const std::string& scene_id,
        const assembly::AssemblyDocument* owner,const assembly::InstancePath& occurrence,
        std::optional<assembly::AssemblyDocument>& display) {
    if(!owner||owner->document_id!=live.active_document_id()||
        owner->effective_history_cursor()==owner->complete_history_order().size())return owner;
    const auto parent=occurrence.parent().value_or(assembly::InstancePath{}).encoded();
    if((scene_id==owner->document_id&&parent.empty())||
        (scene_id==live.displayed_document_id()&&parent==live.active_occurrence_path())) {
        display=owner->history_view_document();return &*display;
    }
    return owner;
}
}
std::vector<std::optional<double>> measurement_face_areas(
        const Workspace& live,const std::string& id,std::span<const kernel::FaceReference> faces) {
    std::vector<std::optional<double>> result(faces.size());kernel::OcctKernel kernel;
    const auto integrate=[&](const kernel::BodyResult& body,const std::string& path) {
        std::vector<kernel::FaceReference> requested;
        std::vector<std::size_t> indices;
        for(std::size_t i=0;i<faces.size();++i)if(!result[i]&&faces[i].instance_path==path) {
            auto local=faces[i];local.instance_path.clear();requested.push_back(std::move(local));indices.push_back(i);
        }
        if(requested.empty())return;
        const auto areas=kernel.measure_face_areas(body,requested);
        for(std::size_t i=0;i<areas.size();++i)if(areas[i])result[indices[i]]=areas[i];
    };
    for(std::size_t i=0;i<faces.size();++i)result[i]=faces[i].measured_area;
    std::set<std::string> paths;for(const auto& face:faces)paths.insert(face.instance_path);
    for(const auto& path:paths) {
        if(path.empty()) {
            if(const auto* part=live.open_part(id)) {
                const auto& cache=part->session.calculated_boundaries();
                for(auto boundary=cache.rbegin();boundary!=cache.rend();++boundary) {
                    integrate(*boundary,path);
                    for(const auto& [body,rows]:boundary->body_boundaries)
                        for(std::size_t row=rows.size();row>0;--row)integrate(rows[row-1],path);
                }
            }
            continue;
        }
        const auto address=live.resolve_occurrence(id,assembly::InstancePath::decode(path));
        if(!address)continue;
        const assembly::AssemblyDocument* owner{};std::optional<assembly::AssemblyDocument> loaded;
        if(const auto* open=live.open_assembly(address->owner_assembly_document_id))owner=&open->session.document();
        else for(auto parent=address->instance_path.parent();parent;parent=parent->parent()) {
            const auto ancestor=live.resolve_occurrence(id,*parent);
            if(!ancestor||ancestor->source_document_id!=address->owner_assembly_document_id)continue;
            const auto file=live.occurrence_source_file(id,*parent);if(!file)break;
            loaded=read_family_assembly(&live,*file,address->owner_assembly_document_id);owner=&*loaded;break;
        }
        std::optional<assembly::AssemblyDocument> display;
        owner=measurement_owner(live,id,owner,address->instance_path,display);
        if(owner)if(const auto* occurrence=owner->find_occurrence(address->occurrence_id)) {
            integrate(occurrence->calculated_source,path);
            // Assembly cuts deliberately retain the original component
            // references. Their persisted input carries that exact geometry.
            for(auto cut=owner->cuts.rbegin();cut!=owner->cuts.rend();++cut)
                if(const auto input=cut->input_component_bodies.find(address->occurrence_id);input!=cut->input_component_bodies.end())
                    integrate(input->second,path);
        }
        // Open sources are authoritative. Their cached local history also
        // retains original references needed by a measurement of an older face.
        if(const auto* part=live.open_part(address->source_document_id)) {
            const auto& cache=part->session.calculated_boundaries();
            for(auto boundary=cache.rbegin();boundary!=cache.rend();++boundary) {
                integrate(*boundary,path);
                for(const auto& [body,rows]:boundary->body_boundaries)
                    for(std::size_t row=rows.size();row>0;--row)integrate(rows[row-1],path);
            }
        }
    }
    return result;
}
kernel::ViewerMesh measurement_scene(const Workspace& live,const std::string& id) {
    std::optional<kernel::ViewerMesh> history_scene;
    if(const auto* active=live.open_assembly(live.active_document_id());active&&
        active->session.document().effective_history_cursor()<active->session.document().complete_history_order().size()) {
        const auto display=active->session.document().history_view_document();
        if(id==display.document_id)history_scene=display.build_scene();
        else if(id==live.displayed_document_id()&&!live.active_occurrence_path().empty())
            history_scene=build_scene_with_assembly_override(live,id,assembly::InstancePath::decode(live.active_occurrence_path()),display);
    }
    auto scene=history_scene?std::move(*history_scene):live.authoritative_viewer_mesh(id);
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
    if(!geometry && reference.instance_path.empty() &&
       (reference.kind==kernel::MeasurementKind::Plane ||
        reference.kind==kernel::MeasurementKind::Axis || reference.kind==kernel::MeasurementKind::Point)) {
        if(const auto* part=live.open_part(id)) {
            const auto& rows=part->session.document().body_properties;
            const auto row=std::ranges::find_if(rows,[&](const auto& value){return value.id+":origin"==reference.owner_id;});
            if(row!=rows.end()) {
                // Analysis frames are measurement-only references. Keep them
                // out of the shared placement packet to avoid dependency cycles.
                auto visible=*row; visible.visible=true;
                geometry=measurement::measure_entity(document::body_properties_origin(visible),reference);
            }
        }
    }
    if(reference.kind==kernel::MeasurementKind::Object && reference.owner_id.empty()&&!reference.instance_path.empty()){
        // Resolve metadata for this exact occurrence, without calculating its source.
        const auto address=live.resolve_occurrence(id,assembly::InstancePath::decode(reference.instance_path));
        if(geometry&&address) {
            const assembly::AssemblyDocument* owner{};
            std::optional<assembly::AssemblyDocument> loaded;
            if(const auto* open=live.open_assembly(address->owner_assembly_document_id))owner=&open->session.document();
            else {
                // Inspect the selected occurrence's saved calculation even if
                // its immediate owner has no open tab. Skip virtual Pattern
                // groups: only a real Assembly can own the selected occurrence.
                // Native reads neither open a tab nor invoke the kernel/solver.
                for(auto parent=address->instance_path.parent();parent;parent=parent->parent()) {
                    const auto ancestor=live.resolve_occurrence(id,*parent);
                    if(!ancestor||ancestor->source_kind!=assembly::ComponentSourceKind::Assembly||
                       ancestor->source_document_id!=address->owner_assembly_document_id)continue;
                    try {
                        const auto file=live.occurrence_source_file(id,*parent);
                        if(!file||file->empty())return {};
                        loaded=read_family_assembly(&live,*file,address->owner_assembly_document_id);
                        owner=&*loaded;
                    } catch(const std::exception&) {
                        // The native dependency may disappear or be replaced
                        // after Open. Use the existing missing-reference UI.
                        return {};
                    }
                    break;
                }
            }
            std::optional<assembly::AssemblyDocument> display;
            owner=measurement_owner(live,id,owner,address->instance_path,display);
            if(owner)if(const auto* item=owner->find_occurrence(address->occurrence_id)) {
                if(item->source_document_id==address->source_document_id&&!item->source_missing) {
                    auto analyzed=item->calculated_source.get();
                    if(!analyzed.physical_properties_current)kernel::OcctKernel{}.ensure_body_properties(analyzed);
                    geometry->values.volume=kernel::MeasurementValue{std::abs(analyzed.volume),false};
                    geometry->values.area=kernel::MeasurementValue{std::abs(analyzed.surface_area),false};
                    if(const auto mass=assembly::occurrence_mass_kg(*item))geometry->values.mass=kernel::MeasurementValue{*mass,false};
                }
            }
        }
    }
    if(!geometry)return {};
    if(std::ranges::any_of(geometry->source_faces,[](const auto& face){return !face.measured_area;})) {
        const auto areas=measurement_face_areas(live,id,geometry->source_faces);
        if(std::ranges::all_of(areas,[](const auto& area){return area.has_value();})) {
            double area=0;for(const auto& value:areas)area+=*value;
            geometry->values.area=kernel::MeasurementValue{area,false};
        }
    }
    if(const auto* part=live.open_part(id)){
        if(reference.kind==kernel::MeasurementKind::Object){
            // A single-owner snapshot is the selected source solid. Never use
            // a later multi-feature total as the volume of an earlier object.
            for(const auto& result:part->session.calculated_boundaries()){
                const auto& faces=result.mesh.original_references.triangle_references;
                if(!faces.empty()&&std::ranges::all_of(faces,[&](const auto& ref){return ref.owner_id==reference.owner_id;})){
                    auto analyzed=result;
                    if(!analyzed.physical_properties_current)kernel::OcctKernel{}.ensure_body_properties(analyzed);
                    geometry->values.volume=kernel::MeasurementValue{std::abs(analyzed.volume),false};
                    geometry->values.area=kernel::MeasurementValue{std::abs(analyzed.surface_area),false};
                    break;
                }
            }
        }
        if(geometry->values.volume)if(const auto density=document::material_density_kg_mm3(part->session.document()))
            geometry->values.mass=kernel::MeasurementValue{geometry->values.volume->value * *density,geometry->values.volume->approximate};
    }
    return geometry;
}
}
