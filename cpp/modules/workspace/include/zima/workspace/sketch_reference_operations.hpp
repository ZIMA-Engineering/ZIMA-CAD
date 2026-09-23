#pragma once
#include <zima/workspace/sketch_operations.hpp>

namespace zima::workspace {
// Project persisted ZIMA viewer data into a Sketch; never calls the solid kernel.
// Shared by interactive reference picking and explicit command input.
void populate_external_reference_cache(const sketcher::Sketch&,
    sketcher::SketchExternalReference&,const kernel::ViewerReferenceGeometry&);
// Supply draft_body_id only for a dialog-owned Part Sketch. Projection uses
// the existing Body coordinate transform; no placement or history is changed.
// draft_section identifies an uncommitted Section Sketch in document coordinates.
[[nodiscard]] sketcher::SketchExternalReference prepare_sketch_external_reference(
    const Workspace&,const std::string& document,const sketcher::Sketch&,
    sketcher::ExternalReferenceKind,const std::string& owner,const std::string& key,
    const std::string& instance_path, const std::string& draft_body_id = {},
    bool draft_section = false, bool body_edge = false);
[[nodiscard]] kernel::ViewerReferenceGeometry part_sketch_body_reference_geometry(
    const document::DocumentSession&,const sketcher::Sketch&,const std::string& draft_body_id = {});
[[nodiscard]] kernel::ViewerReferenceGeometry assembly_sketch_body_reference_geometry(
    const assembly::AssemblyDocument&,const sketcher::Sketch&);
[[nodiscard]] kernel::ViewerReferenceGeometry context_sketch_body_reference_geometry(
    const Workspace&,const std::string& top,const assembly::InstancePath& dependent,
    const std::string& source_document);
// A contextual reference is editable only at its exact active source occurrence.
void require_sketch_reference_context(const Workspace&,const std::string& document,
    const sketcher::SketchExternalReference&);
// Explicitly refresh this Sketch from its selected original/body sources, including
// the exact active Part context. Never regenerates a body or Assembly mate.
// Missing identities remain broken; they are never guessed or rebound.
[[nodiscard]] bool refresh_sketch_reference_snapshot(
    const Workspace&,const std::string& document,sketcher::Sketch&);
}
