#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/feature_sketches.hpp>
#include <algorithm>

namespace zima::workspace {
void apply_sketch_geometry(sketcher::Sketch& draft,const SketchMutation& mutation) {
    mutation(draft);draft.refresh_curve_dependencies();draft.validate();
}
namespace {
template<class Document,class Visitor> void visit_sketches(const Document& document,const Visitor& visit) {
    for(const auto& sketch:document.sketches)if(!visit(sketch))return;
    bool more=true;
    const auto feature=[&](const auto& container) {
        document::visit_feature_sketches(container,[&](const auto& data,std::size_t) {
            if(more)more=visit(sketcher::Sketch::from_serialized(data));
        });
    };
    if constexpr(requires{document.history;})for(const auto& container:document.history) {if(!more)return;feature(container);}
    if constexpr(requires{document.cuts;})for(const auto& cut:document.cuts) {if(!more)return;feature(cut.definition);}
    if(more)for(const auto& section:document.sections)if(!visit(section.sketch))return;
}
void require_editable(const document::PartDocument& document,const std::string& owner) {
    if(const auto* body=document.body_owner_for_object(owner)) {
        if(body->derived_copy)throw SketchOperationError("read_only_body","A derived Body cannot be edited directly.");
        if(body->scope.id!=document.body_history.active_body_id())throw SketchOperationError("inactive_body","Activate the owning Body before editing its history.");
    }
}
void require_editable(const assembly::AssemblyDocument&,const std::string&) {}
template<class Document> bool mutate(Document& document,const std::string& id,const SketchMutation& mutation) {
    const auto apply=[&](sketcher::Sketch& sketch,const std::string& owner) {
        require_editable(document,owner);apply_sketch_geometry(sketch,mutation);
    };
    for(auto& sketch:document.sketches)if(sketch.id==id) {
        apply(sketch,sketch.owner_container_id.empty()?id:sketch.owner_container_id);return true;
    }
    bool found=false;
    const auto feature=[&](auto& container) {
        document::visit_feature_sketches(container,[&](auto& data,std::size_t) {
            if(found)return;auto sketch=sketcher::Sketch::from_serialized(data);
            if(sketch.id!=id)return;apply(sketch,container.id);data=sketch.serialized();found=true;
        });
    };
    if constexpr(requires{document.history;})for(auto& container:document.history)feature(container);
    if constexpr(requires{document.cuts;})for(auto& cut:document.cuts)feature(cut.definition);
    if(found)return true;
    // Section sketches require their section's explicit calculation transaction.
    // Their geometry is readable, but ordinary Sketch mutation cannot publish
    // an out-of-date cutting result.
    return false;
}
}
void visit_document_sketches(const Workspace& live,const std::string& id,const std::function<bool(const sketcher::Sketch&)>& visit) {
    if(const auto* part=live.open_part(id))visit_sketches(part->session.document(),visit);
    else if(const auto* assembly=live.open_assembly(id))visit_sketches(assembly->session.document(),visit);
    else throw SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
}
sketcher::Sketch document_sketch(const Workspace& live,const std::string& id,const std::string& sketch_id) {
    std::optional<sketcher::Sketch> result;
    visit_document_sketches(live,id,[&](const auto& sketch) {if(sketch.id!=sketch_id)return true;result=sketch;return false;});
    if(!result)throw SketchOperationError("sketch_not_found","The requested Sketch does not exist.");
    return std::move(*result);
}
bool mutate_document_sketch(Workspace& live,const std::string& id,const std::string& sketch_id,const SketchMutation& mutation) {
    const auto before=document_sketch(live,id,sketch_id).serialized();
    const auto change=[&](auto& next) {
        if(!mutate(next,sketch_id,mutation))throw SketchOperationError("unsupported_sketch","Edit this Sketch through its owning section operation.");
    };
    if(auto* part=live.open_part(id)) {
        auto next=part->session.document();change(next);
        std::string after;visit_sketches(next,[&](const auto& sketch){if(sketch.id!=sketch_id)return true;after=sketch.serialized();return false;});
        if(after==before)return false;
        part->session.commit(std::move(next),part->session.calculated_boundaries());return true;
    }
    auto* assembly=live.open_assembly(id);
    if(!assembly)throw SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
    auto next=assembly->session.document();change(next);
    std::string after;visit_sketches(next,[&](const auto& sketch){if(sketch.id!=sketch_id)return true;after=sketch.serialized();return false;});
    if(after==before)return false;
    assembly->session.commit(std::move(next));return true;
}
void insert_new_sketch(document::PartDocument& document,sketcher::Sketch sketch,document::HistoryContainer container) {
    if(container.feature_kind!=document::FeatureKind::Sketch || sketch.owner_container_id!=container.id || container.id.empty())
        throw SketchOperationError("invalid_sketch_owner","A new Part Sketch must have its own Sketch container.");
    if(std::ranges::any_of(document.sketches,[&](const auto& existing){return existing.id==sketch.id;}) || document.find_container(container.id))
        throw SketchOperationError("duplicate_sketch","The Sketch identity already exists.");
    sketch.validate();document.insert_history_entry(document::PartHistoryKind::Feature,container.id);
    document.history.push_back(std::move(container));document.sketches.push_back(std::move(sketch));
}
void insert_new_sketch(assembly::AssemblyDocument& document,sketcher::Sketch sketch) {
    if(!sketch.owner_container_id.empty())throw SketchOperationError("invalid_sketch_owner","A standalone Assembly Sketch cannot own a cut container.");
    if(std::ranges::any_of(document.sketches,[&](const auto& existing){return existing.id==sketch.id;}))
        throw SketchOperationError("duplicate_sketch","The Sketch identity already exists.");
    sketch.validate();document.sketches.push_back(std::move(sketch));
}
std::string create_document_sketch(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,std::string name,sketcher::SketchPlane plane) {
    if(name.empty() || name.size()>1024)throw SketchOperationError("invalid_name","A Sketch name must contain 1 to 1024 UTF-8 bytes.");
    auto sketch=sketcher::Sketch::create_default();sketch.name=std::move(name);sketch.plane=plane;sketch.refresh_default_frame();
    const auto sketch_id=sketch.id;
    if(auto* part=live.open_part(id)) {
        auto container=document::PartDocument::create_sketch_container();container.name=sketch.name;sketch.owner_container_id=container.id;
        auto next=part->session.document();insert_new_sketch(next,std::move(sketch),std::move(container));
        PartCalculationPolicy policy;policy.reject_errors=true;
        auto calculated=calculate_part_with_resolved_references(kernel,next,&part->session.calculated_boundaries(),policy);
        part->session.commit(std::move(next),std::move(calculated));return sketch_id;
    }
    if(auto* assembly=live.open_assembly(id)) {
        auto next=assembly->session.document();insert_new_sketch(next,std::move(sketch));next.resolve_constructions();
        assembly->session.commit(std::move(next));return sketch_id;
    }
    throw SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
}
}
