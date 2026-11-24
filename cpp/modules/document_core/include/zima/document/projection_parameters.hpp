#pragma once

#include <zima/document/profile_parameters.hpp>

namespace zima::document {

enum class ProjectionOutput { Curves, Split, Surface, Relief };
enum class ProjectionDirection { SketchNormal, Reference };

struct ProjectionRegion {
    kernel::FaceReference target;
    // A first local pick is expressed in the target owner's frame; an external
    // pick uses the destination Body frame, like its captured target packet. After explicit
    // calculation, the boundary-parent key owns region identity; the seed
    // must never silently select another region when that key disappears.
    kernel::Vec3 seed;
    std::string boundary_key;
    bool operator==(const ProjectionRegion&) const = default;
};

struct ProjectionParameters {
    ProjectionOutput output{ProjectionOutput::Curves};
    std::vector<ExtrusionParameters::EndTarget> targets;
    ProjectionDirection direction{ProjectionDirection::SketchNormal};
    // Axis and directed straight-edge references share the existing native
    // owner/key/occurrence identity. This is not a placement constraint.
    kernel::EdgeReference direction_reference;
    bool reverse{};
    std::vector<ProjectionRegion> regions;
    double height{1}; // Positive normal height; the Feature owns the material sign.
    bool operator==(const ProjectionParameters&) const = default;
};

void validate_projection_parameters(const ProjectionParameters&);
[[nodiscard]] nlohmann::json serialize_projection_parameters(const ProjectionParameters&);
[[nodiscard]] ProjectionParameters load_projection_parameters(const nlohmann::json&);

}
