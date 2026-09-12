#pragma once
#include <zima/document/relations.hpp>
#include <map>
#include <string>
#include <vector>
namespace zima::document {
struct MaterialData {
    std::map<std::string,std::string> properties,units;
    std::map<std::string,std::map<std::string,std::string>> descriptions;
    bool operator==(const MaterialData&) const = default;
};
struct FamilyInstance {
    std::string name;
    std::map<std::string,std::string> values;
    bool operator==(const FamilyInstance&) const = default;
};
struct FamilyTable {
    std::vector<std::string> columns;
    std::vector<FamilyInstance> instances;
    bool operator==(const FamilyTable&) const = default;
};
void validate_material(const MaterialData&);
[[nodiscard]] std::vector<std::string> material_unit_choices(const std::string& property);
void validate_family_table(const FamilyTable&,const std::string& generic_name);
[[nodiscard]] FamilyTable parse_family_table(const std::string&);
[[nodiscard]] std::string serialize_family_table(const FamilyTable&);
}
