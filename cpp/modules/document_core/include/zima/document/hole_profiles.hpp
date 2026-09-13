#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
// Update numeric geometry in the existing owned Sketches. Keeps their identities,
// constraints and external references. Axial profile depth follows the bore;
// performs no kernel calculation.
void update_hole_profiles(HoleParameters&);
}
