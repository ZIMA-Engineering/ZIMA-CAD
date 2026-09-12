#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/document/engineering_metadata.hpp>
namespace zima::workspace {
struct RelationData {
    std::vector<document::ModelRelation> relations;
    std::map<std::string,std::string> parameters;
    std::map<std::string,double> model_values;
    int decimal_places{};
};
[[nodiscard]] RelationData model_relations(const Workspace&,const std::string&);
[[nodiscard]] bool set_model_relations(Workspace&,const std::string&,std::vector<document::ModelRelation>);
[[nodiscard]] document::MaterialData material_data(const Workspace&,const std::string&);
[[nodiscard]] bool set_material_data(Workspace&,const std::string&,document::MaterialData);
[[nodiscard]] document::FamilyTable family_table(const Workspace&,const std::string&);
[[nodiscard]] bool set_family_table(Workspace&,const std::string&,document::FamilyTable);
}
