#pragma once
#include <zima/document/mech_keyway.hpp>
#include <string_view>

namespace zima::document {
struct KeywayCatalogRow {
    std::string id;
    double diameter_over{}, diameter_through{}, width{}, height{}, shaft_depth{}, hub_depth{}, bottom_radius{}, disc_diameter{};
    double shaft_dimension_upper{},shaft_dimension_lower{},hub_dimension_upper{},hub_dimension_lower{};
    std::string shaft_width_fit,hub_width_fit,shaft_dimension_fit,hub_dimension_fit;
    std::string retaining_thread;
    double retaining_depth{};
    std::optional<std::array<double,2>> one_screw_lengths,two_screw_lengths;
};
struct KeywayCatalog {
    std::string id,source,review;
    KeywayType type;
    std::vector<KeywayCatalogRow> rows;
};
[[nodiscard]] const std::vector<KeywayCatalog>& keyway_catalogues();
[[nodiscard]] const KeywayCatalogRow& keyway_size(std::string_view catalogue,
    double diameter,std::string_view preferred_row = {});
// Reevaluate only at explicit preparation/regeneration. Width is controlled by
// the selected catalogue, while length, placement and angle remain authored.
void select_keyway_size(MechKeywayParameters&,double diameter);
}
