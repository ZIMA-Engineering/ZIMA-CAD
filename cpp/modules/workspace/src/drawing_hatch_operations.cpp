#include <zima/workspace/drawing_hatch_operations.hpp>
#include <algorithm>
#include <cctype>
namespace zima::workspace {
namespace {
using Error=DrawingOperationError;
void validate(const document::SectionComponent& value) {
    if(value.mode<0||value.mode>2)throw Error("invalid_arguments","Neplatný režim komponenty řezu.");
    try{document::validate_hatch(value.hatch);}catch(const std::exception& e){throw Error("invalid_arguments",e.what());}
}
void assign(auto& model,const document::SectionDefinition& section) {
    const auto found=std::ranges::find(model.sections,section.id,&document::SectionDefinition::id);
    if(found==model.sections.end())throw Error("section_not_found","Source Section no longer exists");
    found->components=section.components;
}
template<class State> auto open_commit(Workspace* live,const std::string& id,const State& state,document::SectionDefinition section) {
    return [live,id,section=std::move(section),revision=state.session.revision(),generation=state.session.data_generation(),identity=state.runtime_identity]() {
        auto* target=[&](){if constexpr(std::is_same_v<State,PartState>)return live->open_part(id);else return live->open_assembly(id);}();
        if(!target||target->runtime_identity!=identity||target->session.revision()!=revision||target->session.data_generation()!=generation)
            throw Error("stale_edit","The source model changed while hatch properties were open.");
        auto model=target->session.document();assign(model,section);
        if constexpr(std::is_same_v<State,PartState>)target->session.commit(std::move(model),target->session.calculated_boundaries());
        else target->session.commit(std::move(model));
        return true;
    };
}
}
document::SectionDefinition drawing_source_section(const Workspace* live,const drawing::DrawingView& view,const std::filesystem::path& drawing_path) {
    if(view.section_id.empty())throw Error("section_not_found","This drawing view has no Section.");
    auto path=view.source_path;if(!path.empty()&&path.is_relative()&&!drawing_path.empty())path=drawing_path.parent_path()/path;
    const auto sections=source_sections(live,view.source_document_id,path);
    const auto found=std::ranges::find(sections,view.section_id,&document::SectionDefinition::id);
    if(found==sections.end())throw Error("section_not_found","Source Section no longer exists");
    return *found;
}
SectionComponents drawing_section_components(const drawing::DrawingView& view,const document::SectionDefinition& source) {
    auto result=source.components;
    for(const auto& [key,name]:source.component_names)result.try_emplace(key);
    for(auto& [key,value]:result)if(value.mode!=2)value.mode=view.hidden_hatch_components.contains(key)?1:0;
    return result;
}
void set_drawing_section_components(drawing::DrawingView& view,const document::SectionDefinition& source,const SectionComponents& values,bool replace) {
    if(view.section_id!=source.id)throw Error("section_not_found","Source Section no longer exists");
    auto next=source;auto hidden=replace?std::set<std::string>{}:view.hidden_hatch_components;
    for(auto [key,value]:values) {
        if(!source.component_names.contains(key)&&!source.components.contains(key))throw Error("component_not_found","The requested component does not belong to this Section.");
        validate(value);if(value.mode==1)hidden.insert(key);else hidden.erase(key);
        const auto stored=source.components.find(key);
        // Source 3D visibility is independent of hatch visibility in the Drawing.
        if(value.mode!=2)value.mode=stored!=source.components.end()&&stored->second.mode==1?1:0;
        if(value==document::SectionComponent{}&&stored==source.components.end())continue;
        next.components[key]=value;
    }
    view.section_snapshot=std::move(next);view.hidden_hatch_components=std::move(hidden);
}
std::function<bool()> prepare_section_component_commit(Workspace* live,const std::string& id,const std::filesystem::path& path,
    const document::SectionDefinition& section,const document::SectionDefinition* expected) {
    const auto sections=source_sections(live,id,path);
    const auto found=std::ranges::find(sections,section.id,&document::SectionDefinition::id);
    if(found==sections.end())throw Error("section_not_found","Source Section no longer exists");
    if(expected&&document::serialize_sections({*found})!=document::serialize_sections({*expected}))
        throw Error("stale_edit","The source model changed while hatch properties were open.");
    for(const auto& [key,value]:section.components) {
        if(!found->component_names.contains(key)&&!found->components.contains(key))throw Error("component_not_found","The requested component does not belong to this Section.");
        validate(value);
    }
    if(found->components==section.components)return []{return false;};
    if(!live)throw Error("source_unavailable","Open the source model in the workspace to edit hatch parameters");
    if(const auto* part=live->open_part(id))return open_commit(live,id,*part,section);
    if(const auto* assembly=live->open_assembly(id))return open_commit(live,id,*assembly,section);
    // Do not open a source tab until the caller has validated the complete edit.
    const auto modified=std::filesystem::last_write_time(path);const auto size=std::filesystem::file_size(path);
    const auto unchanged=[live,id,path,modified,size] {
        if(live->find(id)||live->document_id_for_path(path)||std::filesystem::last_write_time(path)!=modified||std::filesystem::file_size(path)!=size)
            throw Error("stale_edit","The source model changed while hatch properties were open.");
    };
    auto extension=path.extension().string();std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".prtz") {
        std::vector<kernel::BodyResult> boundaries;auto model=document::PartDocument::load(path,&boundaries);
        if(model.document_id!=id)throw Error("source_identity","Source model identity changed");
        auto next=model;assign(next,section);
        return [live,id,path,unchanged,model=std::move(model),next=std::move(next),boundaries=std::move(boundaries)]()mutable {
            unchanged();live->add_part(std::move(model),std::move(boundaries),path);
            auto* target=live->open_part(id);target->session.commit(std::move(next),target->session.calculated_boundaries());return true;
        };
    }
    if(extension==".asmz") {
        auto model=assembly::AssemblyDocument::load(path);if(model.document_id!=id)throw Error("source_identity","Source model identity changed");
        auto next=model;assign(next,section);
        return [live,id,path,unchanged,model=std::move(model),next=std::move(next)]()mutable {
            unchanged();live->add_assembly(std::move(model),path);live->open_assembly(id)->session.commit(std::move(next));return true;
        };
    }
    throw Error("source_unavailable","Source model is unavailable");
}
}
