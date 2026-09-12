#pragma once
#include <zima/workspace/sketch_operations.hpp>

namespace zima::workspace {
// Project persisted ZIMA viewer data into a Sketch; never calls the solid kernel.
// Shared by interactive reference picking and explicit command input.
void populate_external_reference_cache(const sketcher::Sketch&,
    sketcher::SketchExternalReference&,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] sketcher::SketchExternalReference prepare_sketch_external_reference(
    const Workspace&,const std::string& document,const sketcher::Sketch&,
    sketcher::ExternalReferenceKind,const std::string& owner,const std::string& key,
    const std::string& instance_path);
// Explicitly refresh only this Sketch from the document's calculated snapshot.
// Missing original identities remain broken; they are never guessed or rebound.
[[nodiscard]] bool refresh_sketch_reference_snapshot(
    const Workspace&,const std::string& document,sketcher::Sketch&);
}
