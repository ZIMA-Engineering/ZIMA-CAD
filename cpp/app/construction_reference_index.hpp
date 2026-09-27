#pragma once
#include <zima/document/part_document.hpp>
#include <zima/workspace/reference_index.hpp>

namespace zima::app {
// Tree diagnostics need persisted datum identities even when their planes and
// axes are hidden. This adds no display/picking geometry and performs no solve.
inline void add_construction_origin_references(workspace::ReferenceIndex& index,
        const document::ConstructionObject& object) {
    if(object.suppressed || !object.reference_valid)return;
    const auto& origin=object.container_origin;
    if(!origin.id.empty())for(const auto& child:origin.children) {
        if(child.parent_id==origin.id && !child.key.empty())
            index.keys.emplace(std::string{},origin.id,"origin:"+child.key);
    }
    for(const auto& point:object.curve_points)
        add_construction_origin_references(index,point);
}
}
