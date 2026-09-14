#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
// Replace one exact active Assembly occurrence using already calculated data.
// An optional local display mesh includes transient Sketcher/property overlays.
[[nodiscard]] kernel::ViewerMesh build_scene_with_assembly_override(const Workspace&,
    const std::string& top, const assembly::InstancePath&, const assembly::AssemblyDocument&,
    const kernel::ViewerMesh* display = nullptr);
}
