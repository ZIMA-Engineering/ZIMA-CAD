#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace zima::document {
class PartDocument;
struct SheetMetalDefaults {
    std::optional<double> thickness_mm;
    double k_factor{0.5};
    bool operator==(const SheetMetalDefaults&) const = default;
};
void validate_sheet_metal_defaults(const SheetMetalDefaults&);
[[nodiscard]] SheetMetalDefaults sheet_metal_defaults(const PartDocument&);
void set_sheet_metal_defaults(PartDocument&, const SheetMetalDefaults&);
struct UserParameterData {
    std::map<std::string,std::string> flat;
    std::vector<std::string> order;
    std::map<std::string,std::map<std::string,std::string>> labels,values;
    bool operator==(const UserParameterData&) const = default;
};
struct FileSettingsData {
    std::map<std::string,std::string> units,precision;
    std::optional<SheetMetalDefaults> sheet_metal;
    bool operator==(const FileSettingsData&) const = default;
};
// Canonicalizes shared values for expression lookup; localized values remain
// separate and preserve their language keys and the explicit display order.
void validate_native_metadata_text(const std::string&);
void normalize_user_parameters(UserParameterData&);
void validate_file_settings(const FileSettingsData&);
[[nodiscard]] const std::map<std::string,std::vector<std::string>>& file_unit_choices();
}
