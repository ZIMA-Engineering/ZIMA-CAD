#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/document/metadata.hpp>
#include <set>
namespace zima::workspace {
ComponentRemovalDependencies component_removal_dependencies(const assembly::AssemblyDocument& doc,const std::string& occurrence) {
    if(!doc.find_occurrence(occurrence))throw ComponentOperationError("occurrence_not_found","The requested component occurrence does not exist.");
    const auto uses=[&](const assembly::InstancePath& path){return !path.occurrence_ids.empty()&&path.occurrence_ids.front()==occurrence;};
    ComponentRemovalDependencies result;
    for(const auto& component:doc.components)for(const auto& row:component.placement_references)
        if(uses(row.component_reference.instance_path)||uses(row.target_reference.instance_path))result.placement_components.insert(component.occurrence_id);
    for(const auto& edge:doc.dependencies)
        if(edge.prerequisite_occurrence_id==occurrence&&edge.dependent_occurrence_id!=occurrence)result.dependency_components.insert(edge.dependent_occurrence_id);
    for(const auto& container:doc.sketch_containers)for(const auto& reference:container.placement.references) {
        if(reference.instance_path.empty()||!uses(assembly::InstancePath::decode(reference.instance_path)))continue;
        for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==container.id)result.sketches.insert(sketch.id);
    }
    for(const auto& sketch:doc.sketches)for(const auto& reference:sketch.external_references) {
        // Root Assembly references carry a local occurrence path without an
        // in-context dependent Part. Both that form and explicit own context
        // must prevent removing the referenced component.
        if((!reference.context_assembly_document_id.empty()&&reference.context_assembly_document_id!=doc.document_id)||reference.source_instance_path.empty())continue;
        try{if(uses(assembly::InstancePath::decode(reference.source_instance_path)))result.sketches.insert(sketch.id);}
        catch(const std::invalid_argument&){/* Preserve the existing GUI removal policy for unreadable references. */}
    }
    return result;
}
std::string insert_component(Workspace& live,const std::string& owner,const std::string& source,const std::optional<std::string>& requested_name) {
    if(!live.open_assembly(owner))throw ComponentOperationError("unsupported_document","Component insertion requires an open owning Assembly.");
    const auto address=live.active_occurrence_path().empty()?std::optional<OccurrenceAddress>{}:
        live.resolve_occurrence(live.displayed_document_id(),assembly::InstancePath::decode(live.active_occurrence_path()));
    if(live.active_document_id()!=owner || (live.displayed_document_id()!=owner && (!address || address->source_document_id!=owner)))
        throw ComponentOperationError("unsupported_context","Activate the owning Assembly before inserting a component.");
    const auto* part=live.open_part(source);const auto* assembly=live.open_assembly(source);
    if(!part && !assembly)throw ComponentOperationError("source_not_open","The component source must be an open Part or Assembly.");
    const auto name=requested_name.value_or(part?part->session.document().name:assembly->session.document().name);
    document::validate_native_metadata_text(name);
    if(name.empty() || name.size()>256)throw ComponentOperationError("invalid_arguments","A component name must contain 1 to 256 bytes.");
    try{require_acyclic_document_dependency(live,owner,source);}
    catch(const DocumentDependencyError& error){throw ComponentOperationError(error.code,error.what());}
    return part?live.insert_open_part(owner,source,name):live.insert_open_assembly(owner,source,name);
}
}
