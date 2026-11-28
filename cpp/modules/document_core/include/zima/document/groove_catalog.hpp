#pragma once
#include <zima/document/mech_groove.hpp>
#include <string_view>

namespace zima::document {
struct GrooveCatalogRow {
    std::string id;
    double diameter{}, groove_diameter{}, width_min{}, width_max{}, radius{};
};
struct GrooveCatalog {
    std::string id, standard, source;
    GrooveProfile profile{};
    bool internal{};
    std::vector<GrooveCatalogRow> rows;
};
[[nodiscard]] const std::vector<GrooveCatalog>& groove_catalogues();
[[nodiscard]] const GrooveCatalogRow& groove_size(std::string_view catalogue, double diameter);
void select_groove_size(MechGrooveParameters&);
}
