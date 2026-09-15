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
    std::set<std::string> value_locks;
    // Derived from a Part solid's operation. A subtractive copy replaces its
    // source Body with the result of subtracting the additional operands.
    bool subtract_source{};
    bool operator==(const DerivedCopyParameters&) const = default;
};
} // namespace zima::document
