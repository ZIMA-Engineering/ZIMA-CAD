#include <zima/workspace/section_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
std::vector<document::SectionDefinition> editable_sections(const Workspace& live,const std::string& id) {
    if(live.active_document_id()!=id||live.displayed_document_id()!=id)
        throw SectionOperationError("unsupported_context","Activate the displayed model before changing Sections.");
    if(const auto* part=live.open_part(id))return part->session.document().sections;
    if(const auto* assembly=live.open_assembly(id))return assembly->session.document().sections;
    throw SectionOperationError("unsupported_document","Section operations require an open Part or Assembly.");
}
void commit(Workspace& live,const std::string& id,std::vector<document::SectionDefinition> sections) {
    if(const auto* part=live.open_part(id)) {
        auto next=part->session.document();next.sections=std::move(sections);
        commit_part_document(live,id,std::move(next),part->session.calculated_boundaries());
    }else {
        auto* assembly=live.open_assembly(id);auto next=assembly->session.document();next.sections=std::move(sections);
        assembly->session.commit(std::move(next));
    }
}
}
bool activate_section(Workspace& live,const std::string& document_id,const std::string& section_id) {
    const auto id=document_id,object=section_id;auto sections=editable_sections(live,id);
    if(!object.empty()) {
        const auto found=std::ranges::find(sections,object,&document::SectionDefinition::id);
        if(found==sections.end())throw SectionOperationError("section_not_found","The requested Section does not exist in this document.");
        if(!found->placement.reference_valid)throw SectionOperationError("invalid_reference","Section placement has unresolved references.");
        static_cast<void>(document::section_frames(*found));
    }
    bool changed=false;
    for(auto& value:sections){const bool active=value.id==object;changed=changed||value.show_cut!=active;value.show_cut=active;}
    if(!changed)return false;
    commit(live,id,std::move(sections));return true;
}
bool remove_section(Workspace& live,const std::string& document_id,const std::string& section_id) {
    const auto id=document_id,object=section_id;auto sections=editable_sections(live,id);
    if(!std::erase_if(sections,[&](const auto& value){return value.id==object;}))
        throw SectionOperationError("section_not_found","The requested Section does not exist in this document.");
    commit(live,id,std::move(sections));return true;
}
}
