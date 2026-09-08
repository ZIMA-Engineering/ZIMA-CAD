#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/section.hpp>
#include <algorithm>
#include <cctype>
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
} // namespace zima::app
