#pragma once
#include <zima/document/part_document.hpp>
#include <zima/workspace/reference_index.hpp>

namespace zima::app {
// Tree diagnostics query the same persisted reference owners as placement.
// Native Sketch points/curves exist independently of calculated solid bodies.
inline workspace::ReferenceIndex part_reference_index(const document::PartDocument& document,
        const kernel::ViewerReferenceGeometry& calculated = {}) {
    workspace::ReferenceIndex index;
    index.add_geometry(calculated);
    index.add_geometry(document.origin_viewer_mesh().original_references);
    index.add_geometry(document.body_origin_reference_geometry());
    index.add_geometry(document.history_origin_reference_geometry_before(""));
    index.add_geometry(document.construction_viewer_mesh().original_references);
    index.add_geometry(document.sketch_placement_reference_geometry());
    return index;
}
}
