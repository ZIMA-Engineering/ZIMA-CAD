#include <zima/workspace/body_link_operations.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/kernel/stable_id.hpp>

namespace zima::workspace {
BodyLinkSource read_body_link_source(const Workspace& live,const std::filesystem::path& file,const std::string& expected) {
    const auto path=std::filesystem::absolute(file).lexically_normal();
    if(native_document_type(path)!=NativeDocumentType::Part)throw std::invalid_argument("Select a native Part file (.prtz).");
    BodyLinkSource result;result.path=path;
    if(!expected.empty())if(const auto* open=live.open_part(expected)) {
        result.document=open->session.document();result.calculated=open->session.calculated_boundaries();
        if(!open->path.empty())result.path=std::filesystem::absolute(open->path).lexically_normal();
        return result;
    }
    if(const auto id=live.document_id_for_path(path);id&&(expected.empty()||*id==expected)) {
        if(const auto* open=live.open_part(*id)) {
            result.document=open->session.document();result.calculated=open->session.calculated_boundaries();return result;
        }
    }
    if(!expected.empty()) {
        result.document=read_family_part(&live,path,expected,result.calculated);
        if(result.document.document_id!=expected)throw std::invalid_argument("The linked source file belongs to a different Part.");
    } else {
        result.document=document::PartDocument::load(path,&result.calculated);
        if(const auto* open=live.open_part(result.document.document_id)) {
            result.document=open->session.document();result.calculated=open->session.calculated_boundaries();
        }
    }
    return result;
}
std::vector<std::string> body_link_source_bodies(const BodyLinkSource& source) {
    if(source.calculated.empty())return {};
    auto ids=source.document.body_history.available_before(source.document.body_history.order().size());
    const auto& outputs=source.calculated.back().body_outputs;
    std::erase_if(ids,[&](const auto& id){const auto found=outputs.find(id);return found==outputs.end()||found->second->kernel_shape.empty()||!found->second->calculation_errors.empty();});
    return ids;
}
document::BodyLink body_link_from_source(const BodyLinkSource& source,const std::string& id,const std::string& owner) {
    const auto& graph=source.document.body_history;
    const auto available=graph.available_before(graph.order().size());
    if(std::ranges::find(available,id)==available.end()||source.calculated.empty())
        throw std::invalid_argument("The selected source Body is unavailable.");
    const auto found=source.calculated.back().body_outputs.find(id);
    if(found==source.calculated.back().body_outputs.end()||found->second->kernel_shape.empty()||!found->second->calculation_errors.empty())
        throw std::invalid_argument("The linked Body source has no valid calculated geometry.");
    auto result=found->second.get();result.body_boundaries.clear();result.body_inputs.clear();result.body_outputs.clear();
    result.shaft_thread_owner.clear();result.shaft_thread_references={};result.imported_step_topology.clear();
    result.mesh=kernel::scaled_body_mesh(std::move(result.mesh),1,{},owner,"link");
    result.source_fingerprint="link:"+source.document.document_id+":"+id+":"+owner+":"+result.source_fingerprint;
    return {source.document.document_id,id,source.path,std::make_shared<const kernel::BodyResult>(std::move(result))};
}
BodyGraphEdit prepare_body_link_edit(const document::PartDocument& doc,const std::string& id) {
    if(id.empty()&&!doc.body_history.active_body_id().empty())
        throw BodyOperationError("active_body","Finish Body editing before inserting a linked Body.");
    if(!id.empty()&&(!doc.body_history.find(id)||!doc.body_history.find(id)->link))
        throw BodyOperationError("body_not_found","The requested linked Body does not exist.");
    return {doc.document_id,id.empty()?kernel::make_stable_id():id,doc.body_history,doc.body_history};
}
bool commit_body_link(Workspace& live,const kernel::OcctKernel& kernel,const BodyGraphEdit& edit,document::BodyHistory value) {
    auto* state=live.open_part(edit.document_id);
    if(!state||state->session.document().body_history!=edit.original)
        throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
    if(value.scope.id!=edit.object_id||!value.link||value.derived_copy||value.scale||!value.entries.empty()||value.cursor)
        throw std::invalid_argument("A linked Body requires a valid native source and cannot own a modeling history.");
    require_acyclic_document_dependency(live,edit.document_id,value.link->document_id,value.link->source_path);
    value.link=body_link_from_source(read_body_link_source(live,value.link->source_path,value.link->document_id),value.link->body_id,value.scope.id);
    auto next=state->session.document();auto graph=next.body_history;
    if(const auto* initial=graph.find(edit.object_id)) {
        value.suppressed=initial->suppressed;graph.update_body(std::move(value));
    } else static_cast<void>(graph.create_link(std::move(value)));
    if(graph==next.body_history)return false;
    next.set_body_history(std::move(graph));PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    calculated=calculate_part(kernel,next,&calculated,policy);
    state->session.commit(std::move(next),std::move(calculated));return true;
}
void refresh_body_links(const Workspace& live,document::PartDocument& doc,const std::filesystem::path& owning_file) {
    auto graph=doc.body_history;
    std::map<std::pair<std::filesystem::path,std::string>,BodyLinkSource> sources;
    for(const auto& body:doc.body_history.bodies())if(body.link) {
        auto path=body.link->source_path;if(path.is_relative())path=owning_file.parent_path()/path;
        require_acyclic_document_dependency(live,doc.document_id,body.link->document_id,path);
        const auto key=std::pair{path,body.link->document_id};
        if(!sources.contains(key))sources.emplace(key,read_body_link_source(live,path,body.link->document_id));
        auto value=body;value.link=body_link_from_source(sources.at(key),body.link->body_id,body.scope.id);
        graph.update_body(std::move(value));
    }
    if(graph!=doc.body_history)doc.set_body_history(std::move(graph));
}
}
