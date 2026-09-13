#include <zima/workspace/reference_sources.hpp>
namespace zima::workspace {
kernel::ViewerReferenceGeometry context_original_reference_geometry(const Workspace& live,
    const std::string& top_id,const assembly::InstancePath& dependent,
    const std::string& source_document,const OriginalReferenceFilter& filter) {
    if(source_document.empty())throw std::invalid_argument("External reference source document ID is required");
    const auto* top=live.open_assembly(top_id);
    if(!top)throw std::invalid_argument("Occurrence activation requires an open top-level Assembly");
    const auto target=live.resolve_occurrence(top_id,dependent);
    if(!target||target->source_kind!=assembly::ComponentSourceKind::Part)
        throw std::invalid_argument("External reference requires an exact dependent Part occurrence");
    kernel::ViewerReferenceGeometry result;Workspace loaded;
    const auto frame_for=[&](const assembly::InstancePath& samples,const assembly::InstancePath& face,const std::string& prefix) {
        ReferenceFrame frame;frame.instance_prefix=prefix;
        frame.point=[&live,&top_id,dependent,samples](auto p){
            return live.occurrence_point_from_scene(top_id,dependent,live.occurrence_point_to_scene(top_id,samples,p));
        };
        frame.direction=[&live,&top_id,dependent,samples](auto p){
            return live.occurrence_direction_from_scene(top_id,dependent,live.occurrence_direction_to_scene(top_id,samples,p));
        };
        frame.surface_point=[&live,&top_id,dependent,face](const auto&,auto p){
            return live.occurrence_point_from_scene(top_id,dependent,live.occurrence_point_to_scene(top_id,face,p));
        };
        frame.surface_direction=[&live,&top_id,dependent,face](const auto&,auto p){
            return live.occurrence_direction_from_scene(top_id,dependent,live.occurrence_direction_to_scene(top_id,face,p));
        };
        return frame;
    };
    const auto accept=[&](auto kind,const auto& owner,const auto& key,const auto& path){return !filter||filter(kind,owner,key,path);};
    // Loading only the selected source avoids reading unrelated Parts merely
    // to project one edge. Repeated occurrences share this one native source.
    const auto source_workspace=[&](const assembly::InstancePath& path)->const Workspace& {
        if(live.open_part(source_document))return live;
        if(loaded.open_part(source_document))return loaded;
        const auto file=live.occurrence_source_file(top_id,path);
        if(!file||file->empty())throw ReferenceQueryError("source_unavailable","The selected component has no available native source file.");
        std::vector<kernel::BodyResult> boundaries;auto document=document::PartDocument::load(*file,&boundaries);
        if(document.document_id!=source_document)throw ReferenceQueryError("dependency_identity","The component source file belongs to a different document.");
        loaded.add_part(std::move(document),std::move(boundaries),*file);return loaded;
    };
    const auto append_source=[&](const assembly::InstancePath& path,const assembly::PartOccurrence& root,bool derived) {
        const auto encoded=path.encoded();
        if(derived) {
            // A dependent copy owns its already calculated mirrored/patterned
            // geometry. Reading the original Part would lose that operation.
            const auto root_path=assembly::InstancePath{}.child(root.occurrence_id);
            const auto frame=frame_for(root_path,path,root_path.encoded());
            const auto matches=[&](auto kind,const auto& owner,const auto& key,const auto& candidate) {
                return candidate==encoded&&accept(kind,owner,key,candidate);
            };
            append_original_reference_geometry(result,root.calculated_source->mesh.original_references,frame,matches);
            if(root.source_kind==assembly::ComponentSourceKind::Part) {
                document::PartDocument origin;origin.document_id=source_document;
                append_original_reference_geometry(result,origin.origin_viewer_mesh().original_references,frame,matches);
            }
            return;
        }
        const auto& source=source_workspace(path);const auto frame=frame_for(path,path,encoded);
        visit_original_references(source,source_document,[&](const auto& packet,const auto&){
            append_original_reference_geometry(result,packet,frame,accept);return true;
        });
    };
    const auto children=[&](const auto& self,const auto& nodes,const assembly::InstancePath& parent,
        const assembly::PartOccurrence& root,bool derived,std::size_t depth)->void {
        if(depth>256)throw ReferenceQueryError("dependency_limit","The component dependency graph is too large or too deep.");
        for(const auto& node:nodes) {
            const auto path=parent.child(node.occurrence_id);const bool copied=derived||!node.derived_source_id.empty();
            if(node.source_kind==assembly::ComponentSourceKind::Part&&node.source_document_id==source_document)append_source(path,root,copied);
            if(node.source_kind==assembly::ComponentSourceKind::Assembly)self(self,node.children,path,root,copied,depth+1);
        }
    };
    for(const auto& root:top->session.document().components) {
        const auto path=assembly::InstancePath{}.child(root.occurrence_id);const bool derived=root.derived_copy.has_value();
        if(root.source_kind==assembly::ComponentSourceKind::Part&&root.source_document_id==source_document)append_source(path,root,derived);
        if(root.source_kind==assembly::ComponentSourceKind::Assembly)children(children,root.nested_snapshot,path,root,derived,1);
    }
    return result;
}
} // namespace zima::workspace
