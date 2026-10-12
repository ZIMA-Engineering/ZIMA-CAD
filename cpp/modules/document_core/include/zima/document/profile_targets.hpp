#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Consumes only persisted original-reference packets in the owning Body frame.
// No OCCT query or calculation occurs while selecting or inspecting a target.
[[nodiscard]] std::optional<ExtrusionParameters::EndTarget> resolve_profile_target(
    const ExtrusionParameters::EndTarget&,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] bool profile_target_is_datum(const kernel::FaceReference&);
// Ray distance to an already captured target; shared by both Axis owners.
[[nodiscard]] std::optional<double> axis_target_distance(kernel::Vec3 origin,
    kernel::Vec3 direction,const ExtrusionParameters::EndTarget&);
} // namespace zima::document
