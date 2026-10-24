#pragma once
#include <QVector3D>
#include <cmath>

namespace zima::viewer {
// Only a perpendicular view changes the screen/local H/V assignment. Oblique
// navigation keeps the last perpendicular assignment; no relation disappears.
inline bool sketch_hv_swap_for_view(QVector3D x, QVector3D y, bool previous) {
    x.normalize();y.normalize();const auto normal=QVector3D::crossProduct(x,y).normalized();
    if(std::abs(normal.z())<1.F-1.e-5F)return previous;
    const double ordinary=std::abs(x.x())+std::abs(y.y());
    const double swapped=std::abs(x.y())+std::abs(y.x());
    if(std::abs(ordinary-swapped)<1.e-5)return previous;
    return swapped>ordinary;
}
} // namespace zima::viewer
