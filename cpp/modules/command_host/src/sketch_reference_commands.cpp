#include "sketch_command_support.hpp"
#include <zima/workspace/sketch_reference_operations.hpp>

namespace zima::command_host {
using namespace sketch_commands;
namespace {
using Kind=sketcher::ExternalReferenceKind;
const sketcher::SketchExternalReference& reference(const Sketch& s,const std::string& id) {
    const auto found=std::ranges::find(s.external_references,id,&sketcher::SketchExternalReference::id);
    if(found==s.external_references.end())throw workspace::SketchOperationError("reference_not_found","The Sketch external reference does not exist.");
    return *found;
}
Kind kind(const std::string& name) {
    if(name=="edge")return Kind::Edge;if(name=="face")return Kind::Face;if(name=="point")return Kind::Point;if(name=="axis")return Kind::Axis;
    invalid("Reference kind must be face, edge, point or axis.");return Kind::Edge;
}
}
void Host::register_sketch_reference_commands() {
    add_sketch_command({"sketch.reference.create",tr("Project an original reference into a Sketch using its exact persisted identity."),
        {{"kind",true},{"owner",true},{"key",true},{"instance_path",false},{"profile",false,Type::Boolean}}},[this](Sketch& s,const Json& a) {
        const bool profile=a.value("profile",false);const auto source_kind=kind(a["kind"].get<std::string>());
        if(profile&&source_kind!=Kind::Edge)invalid("Profile geometry requires an original edge reference.");
        auto value=workspace::prepare_sketch_external_reference(workspace_,workspace_.active_document_id(),s,source_kind,
            a["owner"].get<std::string>(),a["key"].get<std::string>(),a.value("instance_path",std::string{}));
        const auto id=value.id;const auto source=value.source_document_id;s.add_external_reference(std::move(value));
        Json data={{"reference",id},{"source_document",source}};
        if(profile)data["geometry"]=s.add_external_profile_geometry(id);return data;
    });
    add_sketch_command({"sketch.reference.project",tr("Create native profile geometry linked to an existing projected edge."),{{"reference",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["reference"].get<std::string>();return Json{{"reference",id},{"geometry",s.add_external_profile_geometry(id)}};
    });
    add_sketch_command({"sketch.reference.delete",tr("Detach an external reference while preserving its native profile curves."),{{"reference",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["reference"].get<std::string>();
        if(!reference(s,id).context_assembly_document_id.empty())throw workspace::SketchOperationError("context_reference","Edit an in-context reference in its owning Assembly context.");
        s.remove_geometry(id);return Json{{"reference",id}};
    });
    add_sketch_command({"sketch.reference.refresh",tr("Explicitly refresh a Sketch's references from its document's calculated snapshot."),{}},[this](Sketch& s,const Json&) {
        static_cast<void>(workspace::refresh_sketch_reference_snapshot(workspace_,workspace_.active_document_id(),s));
        Json broken=Json::array();for(const auto& r:s.external_references)if(r.broken)broken.push_back(r.id);
        return Json{{"broken_references",broken},{"reference_count",s.external_references.size()}};
    });
}
}
