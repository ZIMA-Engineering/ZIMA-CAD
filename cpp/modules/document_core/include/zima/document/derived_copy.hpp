#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/pattern_geometry.hpp>
namespace zima::document {
struct DerivedCopyParameters {
    // Ordered native sources copied by one Mirror/Pattern container.
    // The order is authored UI data; calculation groups compatible operands
    // without changing these identities.
    std::vector<std::string> source_ids;
    ConstructionReference reference;
    kernel::MirrorPlane resolved_plane;
    bool reference_valid{true};
    std::optional<kernel::PatternRequest> pattern;
    std::set<std::string> value_locks;
    // Derived from a Part solid's operation. A subtractive copy replaces its
    // source Body with the result of subtracting the additional operands.
    bool subtract_source{};
    bool operator==(const DerivedCopyParameters&) const = default;
};

inline const std::string& primary_derived_copy_source(
        const DerivedCopyParameters& parameters) {
    if(parameters.source_ids.empty())
        throw std::invalid_argument("Copy source is missing.");
    return parameters.source_ids.front();
}

inline bool is_derived_copy_origin_reference(const ConstructionReference& reference,
    const std::string& owner_id,bool pattern) {
    if(!reference.instance_path.empty()||reference.owner_id!=owner_id+":origin"||reference.offset!=0)return false;
    const auto& key=reference.semantic_key;
    return pattern?(key=="origin:axis:x"||key=="origin:axis:y"||key=="origin:axis:z"):
        (key=="origin:plane:xy"||key=="origin:plane:yz"||key=="origin:plane:xz");
}
} // namespace zima::document
