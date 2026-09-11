#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/physical_properties.hpp>
namespace zima::assembly {
inline std::optional<double> occurrence_mass_kg(const PartOccurrence& item) {
    const double volume=std::abs(item.calculated_source->volume);
    if(volume==0)return 0;
    if(item.density_kg_mm3)return volume * *item.density_kg_mm3;
    // A changed heterogeneous subassembly needs a new mass snapshot. Do not
    // pretend a volume fraction is an exact mass fraction after a cut.
    if(item.nested_mass_kg && std::abs(volume-item.mass_volume_mm3)<=1e-8*std::max(1.0,volume))return item.nested_mass_kg;
    return {};
}
inline std::map<std::string,double> physical_values(const AssemblyDocument& doc) {
    double volume=0,area=0,mass=0;bool known=true;
    const auto suppressed=doc.effectively_suppressed_occurrences();
    for(const auto& item:doc.components) {
        if(suppressed.contains(item.occurrence_id))continue;
        volume+=std::abs(item.calculated_source->volume);area+=std::abs(item.calculated_source->surface_area);
        if(const auto value=occurrence_mass_kg(item))mass+=*value;else known=false;
    }
    return zima::document::physical_values_from_totals(doc,volume,area,
        known?std::optional<double>{mass}:std::nullopt);
}
inline void capture_nested_mass(PartOccurrence& item,const AssemblyDocument& source) {
    const auto values=physical_values(source);item.density_kg_mm3.reset();item.nested_mass_kg.reset();
    if(values.contains("model.mass"))item.nested_mass_kg=values.at("model.mass")*zima::document::mass_unit_kg(source.document_units.at("Mass"));
    item.mass_volume_mm3=std::abs(item.calculated_source->volume);
}
} // namespace zima::assembly
