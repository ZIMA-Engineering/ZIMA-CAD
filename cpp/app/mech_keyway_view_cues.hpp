#pragma once
#include <zima/workspace/mech_keyway_operations.hpp>

namespace zima::app {
inline std::vector<kernel::ViewerDimension> mech_keyway_view_dimensions(const workspace::MechKeywayDraft& draft){
    return workspace::mech_keyway_dimensions(draft);
}
}
