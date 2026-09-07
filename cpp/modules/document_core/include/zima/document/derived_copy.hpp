#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/pattern_geometry.hpp>
namespace zima::document {
struct DerivedCopyParameters {
    std::string source_id;
    ConstructionReference reference;
    kernel::MirrorPlane resolved_plane;
    bool reference_valid{true};
    std::optional<kernel::PatternRequest> pattern;
    unsigned linear_axis{}; // Local Origin X, Y, Z; circular axis remains stored.
    bool operator==(const DerivedCopyParameters&) const = default;
};
} // namespace zima::document
