#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <algorithm>
namespace zima::workspace {
void remove_component(Workspace& live,const kernel::OcctKernel& kernel,const std::string& owner,const std::string& occurrence) {
    auto* state=live.open_assembly(owner);
    if(!state)throw ComponentOperationError("unsupported_document","Component commands require an open Assembly.");
    const auto& before=state->session.document();
    if(component_removal_dependencies(before,occurrence).blocked())
        throw ComponentOperationError("component_in_use","Komponenta je použita vazbou, závislostí nebo externí referencí skici.");
    // Preserve the existing derived-copy removal path. Ordinary occurrences
    // obtain fresh pre-cut inputs just as GUI did, but never publish the
    // intermediate regeneration before the entire removal succeeds.
    auto next=before.find_occurrence(occurrence)->derived_copy?before:live.prepare_assembly_calculation(owner);
    if(component_removal_dependencies(next,occurrence).blocked())
        throw ComponentOperationError("component_in_use","Komponenta je použita vazbou, závislostí nebo externí referencí skici.");
    for(auto& cut:next.cuts) {
        std::erase(cut.target_occurrence_ids,occurrence);
        auto& extrusion=cut.definition.extrusion;
        bool lost_target=false;
        if(!extrusion.target_face.instance_path.empty()) {
            try {
                const auto path=assembly::InstancePath::decode(extrusion.target_face.instance_path);
                lost_target=!path.occurrence_ids.empty()&&path.occurrence_ids.front()==occurrence;
            }catch(const std::invalid_argument&){lost_target=true;}
        }
        if(lost_target&&(extrusion.extent==document::ExtrusionExtent::UpToPlane||extrusion.extent==document::ExtrusionExtent::UpToSurface)) {
            extrusion.extent=document::ExtrusionExtent::Blind;extrusion.target_face={};extrusion.target_surface_triangles.clear();
        }
    }
    std::erase_if(next.components,[&](const auto& item){return item.occurrence_id==occurrence;});
    std::erase_if(next.dependencies,[&](const auto& edge){return edge.dependent_occurrence_id==occurrence;});
    next.calculate_placement_references();
    calculate_resolved_assembly_cuts(kernel,next);
    state->session.commit(std::move(next));
}
} // namespace zima::workspace
