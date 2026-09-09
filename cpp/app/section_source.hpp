#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/section.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
namespace zima::app {
inline std::vector<zima::document::SectionDefinition> sections_for_part(const zima::document::PartDocument& doc){
    auto result=doc.sections;
    for(auto& s:result){s.component_names.clear();s.body_owners.clear();for(const auto& body:doc.body_history.bodies()){
        s.component_names[body.scope.id]=body.name;s.body_owners[body.scope.id]=body.scope.id;
        for(const auto& entry:body.entries)s.body_owners[entry.id]=body.scope.id;
    }}return result;
}
inline std::vector<zima::document::SectionDefinition> sections_for_assembly(const zima::assembly::AssemblyDocument& doc){
    auto result=doc.sections;std::map<std::string,std::string> names;
    const auto visit=[&](auto&& self,const auto& nodes,zima::assembly::InstancePath path,std::string label)->void{
        for(const auto& node:nodes){auto child=path.child(node.occurrence_id);auto text=label.empty()?node.name:label+" / "+node.name;
            if(node.children.empty())names[child.encoded()]=text;else self(self,node.children,child,text);}
    };visit(visit,doc.occurrence_snapshot(),{},{});
    for(auto& s:result)s.component_names=names;return result;
}
inline std::vector<zima::document::SectionDefinition> source_sections(
    zima::workspace::Workspace* workspace,const std::string& id,const std::filesystem::path& path){
    if(workspace){if(const auto* p=workspace->open_part(id))return sections_for_part(p->session.document());
        if(const auto* a=workspace->open_assembly(id))return sections_for_assembly(a->session.document());}
    if(workspace&&!path.empty())if(const auto open=workspace->document_id_for_path(path)){
        if(const auto* p=workspace->open_part(*open))return sections_for_part(p->session.document());
        if(const auto* a=workspace->open_assembly(*open))return sections_for_assembly(a->session.document());
    }
    auto extension=path.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".prtz")return sections_for_part(zima::document::PartDocument::load(path));
    if(extension==".asmz")return sections_for_assembly(zima::assembly::AssemblyDocument::load(path));
    return {};
}
// Prepare a metadata-only model transaction. Geometry and other source
// Sections remain untouched; the caller invokes it after validating Drawing OK.
inline std::function<void()> prepare_section_component_commit(
    zima::workspace::Workspace* workspace,const std::string& source_id,
    const std::filesystem::path& path,const zima::document::SectionDefinition& section){
    const auto current=source_sections(workspace,source_id,path);
    const auto found=std::ranges::find(current,section.id,&zima::document::SectionDefinition::id);
    if(found==current.end())throw std::runtime_error("Source Section no longer exists");
    if(found->components==section.components)return []{};
    if(!workspace)throw std::runtime_error("Open the source model in the workspace to edit hatch parameters");
    auto id=source_id;
    if(!workspace->find(id)){
        if(const auto open=workspace->document_id_for_path(path))id=*open;
        else if(path.extension()==".prtz"){
            std::vector<zima::kernel::BodyResult> boundaries;auto model=zima::document::PartDocument::load(path,&boundaries);
            if(model.document_id!=id)throw std::runtime_error("Source model identity changed");
            workspace->add_part(std::move(model),std::move(boundaries),path);
        }else if(path.extension()==".asmz"){
            auto model=zima::assembly::AssemblyDocument::load(path);
            if(model.document_id!=id)throw std::runtime_error("Source model identity changed");
            workspace->add_assembly(std::move(model),path);
        }
    }
    const auto edit=[&](auto& model){
        const auto target=std::ranges::find(model.sections,section.id,&zima::document::SectionDefinition::id);
        if(target==model.sections.end())throw std::runtime_error("Source Section no longer exists");
        target->components=section.components;
    };
    if(const auto* part=workspace->open_part(id)){
        auto model=part->session.document();edit(model);
        return [workspace,id,model=std::move(model)]()mutable{
            auto* target=workspace->open_part(id);if(!target)throw std::runtime_error("Source Part is no longer open");
            target->session.commit(std::move(model),target->session.calculated_boundaries());
        };
    }
    if(const auto* assembly=workspace->open_assembly(id)){
        auto model=assembly->session.document();edit(model);
        return [workspace,id,model=std::move(model)]()mutable{
            auto* target=workspace->open_assembly(id);if(!target)throw std::runtime_error("Source Assembly is no longer open");
            target->session.commit(std::move(model));
        };
    }
    throw std::runtime_error("Source model is unavailable");
}
} // namespace zima::app
