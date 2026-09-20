#pragma once
#include <zima/viewer/dimension_text_layer.hpp>

namespace zima::drawing_render {
// Running dimensions share one datum and spine. Each target owns one arrow
// and an ordinate label perpendicular to the spine; the datum carries a small ring and literal 0.
template<class Project>
viewer::DimensionPresentation chain_dimension_layout(
    const kernel::ViewerDimension& source, Project project, const QFont& font,
    const QString& text, double scale, bool datum = false) {
    viewer::DimensionPresentation result;
    const auto origin = project(source.line_first), target = project(source.line_second);
    const auto direction = viewer::dimension_screen_unit(target-origin);
    const auto tip = datum ? origin : target;
    const auto label = datum ? origin : project(source.label_position.value_or(source.line_second));
    const auto witness = project(datum ? source.witness_first : source.witness_second);
    result.curves.push_back(QPolygonF{witness, tip});
    if (datum) {
        QPolygonF ring;
        for (int i=0;i<=24;++i) {
            const auto angle=i*6.283185307179586/24;
            ring.push_back(tip+QPointF(std::cos(angle),std::sin(angle))*.6*scale);
        }
        result.curves.push_back(ring);
    } else result.arrows.push_back({tip, source.arrows_reversed ? -direction : direction});
    const auto bounds=viewer::dimension_text_box(font,text,.5*scale);
    auto along=QPointF(-direction.y(),direction.x());
    if(along.x() < -1e-9 || (std::abs(along.x())<1e-9 && along.y()>0))along=-along;
    auto outward=tip-witness;
    if(QLineF(QPointF{},outward).length()<1e-9)
        outward=target-project(source.witness_second);
    if(QLineF(QPointF{},outward).length()<1e-9)outward=along;
    // Use the outside half-plane, opposite the reference geometry, and the
    // actual spine normal even when witness endpoints are oblique.
    const auto outside=along*(viewer::dimension_screen_dot(outward,along)<0?-1.:1.);
    const auto center=label+outside*(1.5*scale+bounds.width()/2);
    const auto normal=QPointF(-along.y(),along.x());
    result.text_baseline=center-along*bounds.center().x()-normal*bounds.center().y();
    result.text_angle=std::atan2(along.y(),along.x())*180/std::acos(-1.);
    result.handles={label,origin,target};
    result.valid=true;
    return result;
}
} // namespace zima::drawing_render
