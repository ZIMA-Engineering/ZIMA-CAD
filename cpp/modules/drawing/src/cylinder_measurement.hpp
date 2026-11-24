#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::drawing::detail {
inline constexpr std::string_view cylinder_diameter_prefix="drawing-cylinder-diameter:";
inline kernel::EdgeReference cylinder_diameter_reference(const kernel::FaceReference& face,int side) {
    return {face.owner_id,std::string(cylinder_diameter_prefix)+std::to_string(face.semantic_key.size())+":"+
        face.semantic_key+":side:"+std::to_string(side),face.instance_path};
}
}
