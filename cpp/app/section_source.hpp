#pragma once
#include <zima/workspace/drawing_sources.hpp>
#include <zima/document/section.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
namespace zima::app {
using zima::workspace::sections_for_part;
using zima::workspace::sections_for_assembly;
using zima::workspace::source_sections;
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
