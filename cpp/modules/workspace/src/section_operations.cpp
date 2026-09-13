#include <zima/workspace/section_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/metadata.hpp>
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
SectionEdit prepare_section_edit(const Workspace& live,const std::string& id,const std::string& object) {
    const auto sections=editable_sections(live,id);SectionEdit edit;edit.document_id=id;edit.creating=object.empty();
    const auto stamp=[&](const auto& state){edit.revision=state.session.revision();edit.generation=state.session.data_generation();edit.runtime_identity=state.runtime_identity;};
    if(const auto* part=live.open_part(id))stamp(*part);else stamp(*live.open_assembly(id));
    if(edit.creating) {
        edit.initial=document::create_section();
        for(std::size_t i=0;;++i) {
            const auto tag=i<26?std::string(1,static_cast<char>('A'+i)):std::to_string(i+1);
            edit.initial.name=tag+"–"+tag;
            if(std::ranges::none_of(sections,[&](const auto& s){return s.name==edit.initial.name;}))break;
        }
    }else {
        const auto found=std::ranges::find(sections,object,&document::SectionDefinition::id);
        if(found==sections.end())throw SectionOperationError("section_not_found","The requested Section does not exist in this document.");
        edit.initial=*found;
    }
    if(const auto* part=live.open_part(id))edit.initial=sections_for_part(part->session.document(),{edit.initial}).front();
    else edit.initial=sections_for_assembly(live.open_assembly(id)->session.document(),{edit.initial}).front();
    for(const auto& [key,name]:edit.initial.component_names)edit.initial.components.try_emplace(key);
    return edit;
}
namespace {
kernel::ViewerReferenceGeometry section_reference_geometry(const Workspace& live,const std::string& id,kernel::ViewerReferenceGeometry geometry) {
    if(const auto* part=live.open_part(id)) {
        const auto& doc=part->session.document();
        append_reference_geometry(geometry,doc.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,doc.construction_viewer_mesh().original_references);
        append_reference_geometry(geometry,doc.history_origin_reference_geometry_before({}));
        append_reference_geometry(geometry,doc.body_origin_reference_geometry());
    }else if(const auto* assembly=live.open_assembly(id)) {
        append_reference_geometry(geometry,assembly->session.document().origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,assembly->session.document().construction_viewer_mesh().original_references);
    }else throw SectionOperationError("unsupported_document","Section operations require an open Part or Assembly.");
    return geometry;
}
}
kernel::ViewerReferenceGeometry section_placement_geometry(const Workspace& live,const std::string& id) {
    return section_reference_geometry(live,id,live.authoritative_viewer_mesh(id).original_references);
}
bool commit_section(Workspace& live,const SectionEdit& edit,document::SectionDefinition value) {
    auto sections=editable_sections(live,edit.document_id);
    const auto current=[&](const auto& state){return state.runtime_identity==edit.runtime_identity&&state.session.revision()==edit.revision&&state.session.data_generation()==edit.generation;};
    const bool unchanged=live.open_part(edit.document_id)?current(*live.open_part(edit.document_id)):current(*live.open_assembly(edit.document_id));
    if(!unchanged)throw SectionOperationError("stale_edit","The document changed while Section properties were open.");
    if(value.id!=edit.initial.id||value.sketch.id!=edit.initial.sketch.id||value.container_origin!=edit.initial.container_origin||
        value.sketch.owner_container_id!=value.id||value.sketch.plane_reference_owner_id!=value.container_origin.id)
        throw SectionOperationError("identity_changed","Editing must preserve Section and owned Sketch identities.");
    const auto first=value.name.find_first_not_of(" \t\r\n"),last=value.name.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)throw SectionOperationError("invalid_name","Vyplňte název řezu.");
    value.name=value.name.substr(first,last-first+1);document::validate_native_metadata_text(value.name);
    if(std::ranges::any_of(sections,[&](const auto& s){return s.id!=value.id&&s.name==value.name;}))
        throw SectionOperationError("duplicate_name","Název řezu již existuje.");
    for(const auto& [key,component]:value.components) {
        if(!edit.initial.component_names.contains(key)&&!edit.initial.components.contains(key))
            throw SectionOperationError("component_not_found","The requested component does not belong to this Section.");
        if(component.mode<0||component.mode>2)throw SectionOperationError("invalid_arguments","Neplatný režim komponenty řezu.");
        document::validate_hatch(component.hatch);
    }
    value.component_names=edit.initial.component_names;value.body_owners=edit.initial.body_owners;
    std::vector<document::SectionDefinition> pending{std::move(value)};
    const auto source=live.authoritative_viewer_mesh(edit.document_id);
    document::resolve_section_placements(pending,section_reference_geometry(live,edit.document_id,source.original_references));
    value=std::move(pending.front());
    if(!value.placement.reference_valid)throw SectionOperationError("invalid_reference","Section placement has unresolved references.");
    static_cast<void>(document::calculate_section(source,value));
    if(!edit.creating&&document::serialize_sections({value})==document::serialize_sections({edit.initial}))return false;
    if(value.show_cut)for(auto& section:sections)section.show_cut=false;
    const auto found=std::ranges::find(sections,value.id,&document::SectionDefinition::id);
    if(edit.creating) {
        if(found!=sections.end())throw SectionOperationError("identity_changed","A new Section must have a new identity.");
        sections.push_back(std::move(value));
    }else {
        if(found==sections.end())throw SectionOperationError("section_not_found","The requested Section does not exist in this document.");
        *found=std::move(value);
    }
    commit(live,edit.document_id,std::move(sections));return true;
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
