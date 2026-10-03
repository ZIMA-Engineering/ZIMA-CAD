#pragma once
#include <zima/workspace/family_operations.hpp>
#include <zima/document/relation_program.hpp>

namespace zima::workspace {
struct RelationDimension {
    document::FamilyColumn binding;
    document::RelationInput input;
    double native_scale{1};
};
// Enumerate persisted scalar slots; never traverse OCCT or change placement.
std::map<std::string,RelationDimension> relation_dimensions(const document::PartDocument&);
std::map<std::string,RelationDimension> relation_dimensions(const assembly::AssemblyDocument&);
document::RelationInputs relation_inputs(const document::PartDocument&,const std::map<std::string,double>& physical);
document::RelationInputs relation_inputs(const assembly::AssemblyDocument&,const std::map<std::string,double>& physical);
// Preflight and rewrite a private document draft. Does not change its unit map,
// calculate geometry or commit; the settings transaction owns all publication.
void convert_relation_units(document::PartDocument&,const std::map<std::string,std::string>& new_units);
void convert_relation_units(assembly::AssemblyDocument&,const std::map<std::string,std::string>& new_units);
void apply_relation_dimensions(document::PartDocument&,const std::map<std::string,double>& physical);
void apply_relation_dimensions(assembly::AssemblyDocument&,const std::map<std::string,double>& physical);
void apply_relation_parameters(document::PartDocument&,const std::map<std::string,double>& physical);
void apply_relation_parameters(assembly::AssemblyDocument&,const std::map<std::string,double>& physical);
}
