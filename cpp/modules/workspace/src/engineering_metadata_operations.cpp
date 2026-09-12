#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/precision.hpp>
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
bool set_model_relations(Workspace& live,const std::string& id,std::vector<document::ModelRelation> relations) {
    document::validate_model_relations(relations);const auto before=model_relations(live,id);
    const auto values=document::evaluate_relations(before.parameters,relations,before.model_values,before.decimal_places);
    return metadata_detail::write(live,id,[&](auto& doc) {
        doc.user_parameters=values;doc.relations=std::move(relations);
        for(const auto& relation:doc.relations) {
            doc.user_parameter_values[relation.target][""]=values.at(relation.target);
            if(std::ranges::find(doc.user_parameter_order,relation.target)==doc.user_parameter_order.end())doc.user_parameter_order.push_back(relation.target);
        }
    },[](const auto& a,const auto& b){return a.relations==b.relations && a.user_parameters==b.user_parameters && a.user_parameter_values==b.user_parameter_values && a.user_parameter_order==b.user_parameter_order;});
}
document::MaterialData material_data(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){return material(doc);});
}
bool set_material_data(Workspace& live,const std::string& id,document::MaterialData values) {
    document::validate_material(values);
    std::erase_if(values.units,[](const auto& entry){return entry.second.empty();});
    std::erase_if(values.descriptions,[](const auto& entry){return entry.second.empty();});
    return metadata_detail::write(live,id,[&](auto& doc) {
        doc.physical_parameters=std::move(values.properties);doc.physical_parameter_units=std::move(values.units);
        doc.material_parameter_descriptions=std::move(values.descriptions);
    },[](const auto& a,const auto& b){return material(a)==material(b);});
}
document::FamilyTable family_table(const Workspace& live,const std::string& id) {
    return metadata_detail::read(live,id,[](const auto& doc){return document::parse_family_table(doc.family_table);});
}
bool set_family_table(Workspace& live,const std::string& id,document::FamilyTable values) {
    const auto name=metadata_detail::read(live,id,[](const auto& doc){return doc.name;});
    document::validate_family_table(values,name);
    for(auto& instance:values.instances)for(const auto& column:values.columns)instance.values.try_emplace(column,"");
    const auto serialized=document::serialize_family_table(values);
    return metadata_detail::write(live,id,[&](auto& doc){doc.family_table=serialized;},[](const auto& a,const auto& b){return a.family_table==b.family_table;});
}
}
