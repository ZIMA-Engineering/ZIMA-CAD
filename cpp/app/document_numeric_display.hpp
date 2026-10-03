#pragma once
#include <zima/ui/unit_spin_box.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <numbers>

namespace zima::app {
// Presentation only: metadata changes must update the existing View immediately,
// without calculating geometry, republishing its mesh or rebuilding the Tree.
inline void update_viewer_numeric_context(viewer::MeshView& view,const QWidget& owner) {
    view.set_dimension_decimal_places(ui::numeric_decimal_places(&owner));
    const auto length=ui::document_unit(&owner,"Length","mm").toStdString();
    const auto angle=ui::document_unit(&owner,"Angle","deg");
    view.set_dimension_display_units({document::length_unit_mm(length),length,
        angle=="rad"?180./std::numbers::pi:1.,angle=="rad"?"rad":"°"});
}
} // namespace zima::app
