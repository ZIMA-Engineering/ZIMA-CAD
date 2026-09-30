#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/document/precision.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/metadata.hpp>
#include "metadata_transaction.hpp"
#include <algorithm>
namespace zima::workspace {
namespace {
template<class Doc> document::MaterialData material(const Doc& doc) {
    return {doc.physical_parameters,doc.physical_parameter_units,doc.material_parameter_descriptions};
}
}
RelationData model_relations(const Workspace& live,const std::string& id) {
    auto result=metadata_detail::read(live,id,[](const auto& doc) {
        document::validate_file_settings({doc.document_units,doc.document_precision});
        return RelationData{doc.relations,doc.user_parameters,{},static_cast<int>(document::precision_value(doc.document_precision,"decimal_places",3))};
    });
    if(const auto* part=live.open_part(id))result.model_values=document::physical_values(part->session.document(),part->session.calculated_boundaries());
    else result.model_values=assembly::physical_values(live.open_assembly(id)->session.document());
    return result;
}
bool set_model_relations(Workspace& live,const std::string& id,std::string relations) {
    const document::RelationProgram program(relations);const auto before=model_relations(live,id);
    metadata_detail::read(live,id,[&](const auto& doc){
        // Validate against current persisted values without publishing results
        // or calculating geometry. Regenerate remains the only evaluation commit.
        auto draft=doc;draft.relations=relations;apply_relation_parameters(draft,before.model_values);return true;
    });
    return metadata_detail::write(live,id,[&](auto& doc) {
        doc.relations=std::move(relations);
    },[](const auto& a,const auto& b){return a.relations==b.relations;});
}
document::MaterialData material_data(const Workspace& live,const std::string& id) {
    const auto* part=live.open_part(id);
    if(!part)throw std::invalid_argument("Material requires an open Part document.");
    return material(part->session.document());
}
bool set_material_data(Workspace& live,const std::string& id,document::MaterialData values) {
    auto* part=live.open_part(id);
    if(!part)throw std::invalid_argument("Material requires an open Part document.");
    document::validate_material(values);
    std::erase_if(values.units,[](const auto& entry){return entry.second.empty();});
    std::erase_if(values.descriptions,[](const auto& entry){return entry.second.empty();});
    auto next=part->session.document();
    next.physical_parameters=std::move(values.properties);next.physical_parameter_units=std::move(values.units);
    next.material_parameter_descriptions=std::move(values.descriptions);
    if(material(next)==material(part->session.document()))return false;
    commit_part_document(live,id,std::move(next),part->session.calculated_boundaries());
    return true;
}
document::FamilyTable family_table(const Workspace& live,const std::string& requested) {
    const auto id=family_owner(live,requested);
    return metadata_detail::read(live,id,[](const auto& doc){return document::parse_family_table(doc.family_table);});
}
bool set_family_table(Workspace& live,const std::string& requested,document::FamilyTable values) {
    const auto id=family_owner(live,requested);
    const auto name=metadata_detail::read(live,id,[](const auto& doc){return doc.name;});
    document::validate_family_table(values,name);
    validate_family_references(live,id,values);
    const auto previous=family_table(live,id);
    for(auto& row:values.instances)if(row.id.empty()) {
        const auto old=std::ranges::find(previous.instances,row.name,&document::FamilyInstance::name);
        row.id=old==previous.instances.end()?document::PartDocument::create_default().document_id:old->id;
    }

    for(auto& instance:values.instances)for(const auto& column:values.columns)instance.values.try_emplace(column,"");
    const auto serialized=document::serialize_family_table(values);
    return metadata_detail::write(live,id,[&](auto& doc){doc.family_table=serialized;},[](const auto& a,const auto& b){return a.family_table==b.family_table;});
}
}
