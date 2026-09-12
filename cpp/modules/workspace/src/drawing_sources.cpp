#include <zima/workspace/drawing_sources.hpp>
#include <algorithm>
#include <zima/document/file_path.hpp>
#include <cctype>
namespace zima::workspace {
namespace {
std::string source_extension(const std::filesystem::path& path){auto extension=path.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return extension;}
}
std::vector<zima::document::SectionDefinition> sections_for_part(const zima::document::PartDocument& doc){
    auto result=doc.sections;
    for(auto& s:result){s.component_names.clear();s.body_owners.clear();for(const auto& body:doc.body_history.bodies()){
        s.component_names[body.scope.id]=body.name;s.body_owners[body.scope.id]=body.scope.id;
        for(const auto& entry:body.entries)s.body_owners[entry.id]=body.scope.id;
    }}return result;
}
std::vector<zima::document::SectionDefinition> sections_for_assembly(const zima::assembly::AssemblyDocument& doc){
    auto result=doc.sections;std::map<std::string,std::string> names;
    const auto visit=[&](auto&& self,const auto& nodes,zima::assembly::InstancePath path,std::string label)->void{
        for(const auto& node:nodes){auto child=path.child(node.occurrence_id);auto text=label.empty()?node.name:label+" / "+node.name;
            if(node.children.empty())names[child.encoded()]=text;else self(self,node.children,child,text);}
    };visit(visit,doc.occurrence_snapshot(),{},{});
    for(auto& s:result)s.component_names=names;return result;
}
std::vector<zima::document::SectionDefinition> source_sections(
    const Workspace* workspace,const std::string& id,const std::filesystem::path& path){
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

zima::drawing::TitleBlockContext build_title_block_context_for_source(
    const std::string&, const std::filesystem::path&, const Workspace*);

// Builds BOM rows from the current state of an Assembly source (by open
// workspace document if available, otherwise by loading the .asmz file),
// so both initial view insertion and later view regeneration can rebuild
// the BOM from the assembly's up-to-date component list.
std::vector<zima::drawing::BomRow> build_bom_rows_for_source(
    const std::string& source_id, const std::filesystem::path& source_path,
    const Workspace* workspace) {
    std::vector<zima::drawing::BomRow> bom;
    const zima::assembly::AssemblyDocument* assembly{};
    std::optional<zima::assembly::AssemblyDocument> loaded;
    if (workspace != nullptr) if (const auto* open = workspace->open_assembly(source_id))
        assembly = &open->session.document();
    if (assembly == nullptr && !source_path.empty() && source_extension(source_path) == ".asmz") {
        loaded = zima::assembly::AssemblyDocument::load(source_path); assembly = &*loaded;
    }
    const auto append=[&](const std::string& id,std::filesystem::path path,const std::string& name) {
        if(path.is_relative()&&!source_path.empty())path=source_path.parent_path()/path;
        const auto key=id+"|"+zima::document::path_to_utf8(path.lexically_normal());
        const auto existing=std::ranges::find(bom,key,&zima::drawing::BomRow::designation);
        if(existing!=bom.end()){++existing->quantity;return;}
        auto context=build_title_block_context_for_source(id,path,workspace);
        zima::drawing::BomRow row{static_cast<int>(bom.size()+1),1,name,key,{}};
        row.source_document_id=id;row.source_path=path;
        row.mass_unit=context.mass_unit;
        row.file_stem=context.file_stem;row.parameters=std::move(context.parameters);
        row.parameter_values=std::move(context.parameter_values);row.parameter_aliases=std::move(context.parameter_aliases);
        bom.push_back(std::move(row));
    };
    if(assembly) {
        const auto suppressed=assembly->effectively_suppressed_occurrences();
        for(const auto& component:assembly->components)if(!suppressed.contains(component.occurrence_id))
            append(component.source_document_id,component.source_path,component.name);
    }
    else if((workspace&&workspace->open_part(source_id))||source_extension(source_path)==".prtz")
        append(source_id,source_path.is_relative()?std::filesystem::absolute(source_path):source_path,zima::document::path_to_utf8(source_path.stem()));
    return bom;
}

// Read the authoritative source Parameters without a geometry calculation.
zima::drawing::TitleBlockContext build_title_block_context_for_source(
    const std::string& source_id, const std::filesystem::path& source_path,
    const Workspace* workspace) {
    zima::drawing::TitleBlockContext context;
    context.file_stem = zima::document::path_to_utf8(source_path.stem());
    if(workspace && !workspace->find(source_id))if(const auto id=workspace->document_id_for_path(source_path))
        return build_title_block_context_for_source(*id,source_path,workspace);
    const zima::document::PartDocument* part{};
    std::optional<zima::document::PartDocument> loaded_part;
    if (workspace != nullptr) if (const auto* open = workspace->open_part(source_id))
        part = &open->session.document();
    if (part == nullptr && !source_path.empty() && source_extension(source_path) == ".prtz") {
        try { loaded_part = zima::document::PartDocument::load(source_path); part = &*loaded_part; }
        catch (const std::exception&) { part = nullptr; }
    }
    const auto use_parameters=[&](const auto& document) {
        if(context.file_stem.empty())context.file_stem=document.name;
        context.mass_unit = document.document_units.at("Mass");
        context.parameters = document.user_parameters;
        context.parameter_values = document.user_parameter_values;
        context.parameter_labels = document.user_parameter_labels;
        context.parameter_order = document.user_parameter_order;
        if(context.parameter_order.empty())for(const auto& [key,value]:document.user_parameters)
            context.parameter_order.push_back(key);
        for (const auto& [key, labels] : document.user_parameter_labels)
            for (const auto& [locale, label] : labels)
                if (!label.empty()) context.parameter_aliases[label] = key;
    };
    if (part != nullptr) use_parameters(*part);
    else if(workspace && workspace->open_assembly(source_id))
        use_parameters(workspace->open_assembly(source_id)->session.document());
    else if(!source_path.empty() && source_extension(source_path)==".asmz") {
        const auto assembly=zima::assembly::AssemblyDocument::load(source_path);use_parameters(assembly);
    }
    return context;
}

}
