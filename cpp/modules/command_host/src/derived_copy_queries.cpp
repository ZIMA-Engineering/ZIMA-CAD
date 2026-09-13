#include <zima/command_host/host.hpp>
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/kernel/pattern_geometry.hpp>
namespace zima::command_host {
namespace {
const char* source_kind(workspace::CopySourceKind kind) {
    switch(kind) {
    case workspace::CopySourceKind::Body:return "body";
    case workspace::CopySourceKind::Boolean:return "boolean";
    case workspace::CopySourceKind::Component:return "component";
    }
    throw std::logic_error("Unknown copy source kind");
}
const char* distribution(kernel::PatternDistribution kind) {
    switch(kind) {
    case kernel::PatternDistribution::Forward:return "forward";
    case kernel::PatternDistribution::Reverse:return "reverse";
    case kernel::PatternDistribution::Both:return "both";
    case kernel::PatternDistribution::Symmetric:return "symmetric";
    }
    throw std::logic_error("Unknown pattern distribution");
}
Json xyz(kernel::Vec3 point){return Json::array({point.x,point.y,point.z});}
std::uint64_t revision(const workspace::Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.revision();
    return live.open_assembly(id)->session.revision();
}
Json details(const workspace::Workspace& live,const std::string& id,const std::string& object,bool pattern) {
    const auto copy=workspace::derived_copy_definition(live,id,object);
    if(copy.parameters.pattern.has_value()!=pattern)
        throw workspace::DerivedCopyError("wrong_feature","The requested copy type does not match this object.");
    const auto& p=copy.parameters;const auto& ref=p.reference;
    Json result={{"document",id},{"object",object},{"name",copy.name},{"kind",pattern?"pattern":"mirror"},
        {"source",p.source_id},{"origin",document::create_container_origin(object).id},{"visible",copy.visible},
        {"placement",copy.placement},{"reference_valid",p.reference_valid},{"value_locks",p.value_locks},
        {"reference",{{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path},
            {"offset_mm",ref.offset}}},
        {"revision",revision(live,id)}};
    if(!pattern) {
        result["resolved_plane"]={{"point",xyz(p.resolved_plane.point)},{"normal",xyz(p.resolved_plane.normal)}};
        return result;
    }
    const auto& request=*p.pattern;auto linear=Json::array();
    for(const auto& direction:request.linear)linear.push_back({
        {"axis",direction.local_axis>=0&&direction.local_axis<3?Json(std::string(1,"xyz"[direction.local_axis])):Json(nullptr)},
        {"spacing_mm",direction.spacing},{"count",direction.count},{"reverse_count",direction.reverse_count},
        {"distribution",distribution(direction.distribution)},{"resolved_direction",xyz(direction.direction)}});
    Json count=nullptr;
    try {count=kernel::pattern_instance_count(request);}catch(const std::invalid_argument&){}
    result["pattern"]={{"mode",request.circular?"circular":"linear"},{"count",request.count},
        {"instance_count",std::move(count)},{"full_circle",request.full_circle},{"angle_degrees",request.angle_degrees},
        {"resolved_origin",xyz(request.origin)},{"resolved_axis",xyz(request.axis)},{"linear",std::move(linear)}};
    return result;
}
}
void Host::register_derived_copy_queries() {
    dispatcher_.add({"derived_copy.sources",tr("List available Bodies or immediate components before a derived copy."),
        {{"object",false},{"document",false}},false},[this](const Json& args) {
            try {
                const auto id=args.value("document",workspace_.active_document_id());
                const auto sources=workspace::derived_copy_sources(workspace_,id,args.value("object",std::string{}));
                auto items=Json::array();
                for(const auto& source:sources.items)items.push_back({{"id",source.id},{"name",source.name},
                    {"kind",source_kind(source.kind)},{"visible",source.visible},
                    {"instance_path",source.kind==workspace::CopySourceKind::Component?assembly::InstancePath{}.child(source.id).encoded():std::string{}}});
                return Result::success({{"document",id},{"object",args.value("object",std::string{})},
                    {"boundary",sources.boundary},{"items",std::move(items)},{"revision",revision(workspace_,id)}});
            }catch(const workspace::DerivedCopyError& error){return Result::failure(error.code,tr(error.what()));}
        });
    for(const bool pattern:{false,true})dispatcher_.add({pattern?"pattern.get":"mirror.get",
        pattern?tr("Read persisted Pattern parameters and source identity."):tr("Read persisted Mirror parameters and source identity."),
        {{"object",true},{"document",false}},false},[this,pattern](const Json& args) {
            try {return Result::success(details(workspace_,args.value("document",workspace_.active_document_id()),args.at("object"),pattern));}
            catch(const workspace::DerivedCopyError& error){return Result::failure(error.code,tr(error.what()));}
        });
}
} // namespace zima::command_host
