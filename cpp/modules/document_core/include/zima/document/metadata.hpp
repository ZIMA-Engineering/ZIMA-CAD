#pragma once
#include <map>
#include <string>
#include <vector>
namespace zima::document {
struct UserParameterData {
    std::map<std::string,std::string> flat;
    std::vector<std::string> order;
    std::map<std::string,std::map<std::string,std::string>> labels,values;
    bool operator==(const UserParameterData&) const = default;
};
struct FileSettingsData {
    std::map<std::string,std::string> units,precision;
    bool operator==(const FileSettingsData&) const = default;
};
// Canonicalizes shared values for expression lookup; localized values remain
// separate and preserve their language keys and the explicit display order.
void normalize_user_parameters(UserParameterData&);
void validate_file_settings(const FileSettingsData&);
[[nodiscard]] const std::map<std::string,std::vector<std::string>>& file_unit_choices();
}
