#pragma once

namespace zima::document {
struct ConstructionObject;

// Detach inputs outside the owning route while retaining the resolved point frames.
[[nodiscard]] bool curve_has_external_point_references(const ConstructionObject& curve);
[[nodiscard]] bool release_curve_point_references(ConstructionObject& curve);
}
