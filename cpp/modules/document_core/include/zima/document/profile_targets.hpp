#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Consumes only persisted original-reference packets in the owning Body frame.
// No OCCT query or calculation occurs while selecting or inspecting a target.
[[nodiscard]] std::optional<ExtrusionParameters::EndTarget> resolve_profile_target(
    const ExtrusionParameters::EndTarget&,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] bool profile_target_is_datum(const kernel::FaceReference&);
// Classifies already captured target geometry relative to a resolved profile
// plane without asking OCCT. A target that crosses the plane has no unique
// half-space and deliberately returns no value.
[[nodiscard]] std::optional<int> profile_target_half_space(
    kernel::Vec3 profile_origin,kernel::Vec3 profile_normal,
    const ExtrusionParameters::EndTarget&);
// Ray distance to an already captured target; shared by both Axis owners.
[[nodiscard]] std::optional<double> axis_target_distance(kernel::Vec3 origin,
    kernel::Vec3 direction,const ExtrusionParameters::EndTarget&);
} // namespace zima::document
