#pragma once
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <zima/kernel/dimension_layout.hpp>
namespace zima::viewer {
// One projected packet for painting, picking and grips. Coordinates are screen-like
// (positive Y down), in pixels or paper mm; no solid calculation or value mutation.
struct DimensionPresentation {
    bool valid{}, oblique{}, outside{};
    std::vector<QPolygonF> curves;
    std::vector<std::pair<QPointF, QPointF>> arrows;
    std::array<QPointF, 3> handles{};
    QPointF text_baseline;
    double text_angle{};
};
inline double dimension_screen_dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
inline QPointF dimension_screen_unit(QPointF a) {
    auto n = std::hypot(a.x(), a.y());
    return n > 1e-9 ? a / n : QPointF(1, 0);
}
// Invert the projected annotation plane. At an edge-on view the readable
// measuring direction still permits dragging; use its one-dimensional measure.
inline std::optional<QPointF> dimension_plane_drag(QPointF move,QPointF along,QPointF outward) {
    const double a2=dimension_screen_dot(along,along),b2=dimension_screen_dot(outward,outward);
    const double determinant=along.x()*outward.y()-along.y()*outward.x();
    if(std::abs(determinant)>1e-7*std::max(a2,b2))
        return QPointF((move.x()*outward.y()-move.y()*outward.x())/determinant,
                       (along.x()*move.y()-along.y()*move.x())/determinant);
    if(a2>=b2 && a2>1e-16)return QPointF(dimension_screen_dot(move,along)/a2,0);
    if(b2>1e-16)return QPointF(0,dimension_screen_dot(move,outward)/b2);
    return {};
}
template <class Project>
DimensionPresentation dimension_presentation(const kernel::ViewerDimension &d, Project project,
                                             double text_width, double arrow = 10, double gap = 3) {
    DimensionPresentation out;
    auto a = project(d.line_first), b = project(d.line_second);
    const auto w1 = project(d.witness_first), w2 = project(d.witness_second);
    auto normal = kernel::dimension_unit(d.plane_normal);
    const auto projected_normal = project(kernel::dimension_add(d.witness_first, normal)) - w1;
    auto model_u = kernel::dimension_unit(kernel::dimension_sub(d.witness_second, d.witness_first));
    if (d.kind == kernel::ViewerDimensionKind::Angular)
        model_u = kernel::dimension_unit(kernel::dimension_sub(d.line_first, d.witness_first));
    const auto model_v = kernel::dimension_cross(normal, model_u);
    const double scale =
        std::max(QLineF(w1, project(kernel::dimension_add(d.witness_first, model_u))).length(),
                 QLineF(w1, project(kernel::dimension_add(d.witness_first, model_v))).length());
    out.oblique =
        std::hypot(projected_normal.x(), projected_normal.y()) > std::max(1e-6, scale * .015);
    const bool angular = d.kind == kernel::ViewerDimensionKind::Angular;
    const bool radial = d.kind == kernel::ViewerDimensionKind::Radius ||
                        d.kind == kernel::ViewerDimensionKind::Diameter;
    auto label_position = d.label_position;
    if (radial && label_position) {
        const auto offset = kernel::dimension_sub(*label_position, d.witness_first);
        label_position = kernel::dimension_sub(*label_position,
            kernel::dimension_scale(normal, kernel::dimension_dot(offset, normal)));
    }
    QPolygonF line;
    if (angular) {
        const auto u = kernel::dimension_sub(d.line_first, d.witness_first),
                   v = kernel::dimension_cross(normal, u);
        for (int i = 0; i <= 48; ++i) {
            const double t = d.sweep_degrees * std::numbers::pi * i / (180 * 48);
            line << project(kernel::dimension_add(
                d.witness_first, kernel::dimension_add(kernel::dimension_scale(u, std::cos(t)),
                                                       kernel::dimension_scale(v, std::sin(t)))));
        }
        a = line.front();
        b = line.back();
        out.curves.push_back({w1, a});
        out.curves.push_back({w1, b});
    } else if (radial) {
        b = w2;
        a = d.kind == kernel::ViewerDimensionKind::Diameter ? w1 - (b - w1) : w1;
        line = {a, b};
    } else {
        line = {a, b};
        out.curves.push_back({w1, a});
        out.curves.push_back({w2, b});
    }
    for (auto p : line)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return out;
    for (auto p : {w1, w2})
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return out;
    // Hide only a dimension whose projected measuring line collapses to a point.
    // An edge-on dimension plane alone is not a reason to hide a readable line.
    double projected_span=0;
    for(auto p:line)projected_span=std::max(projected_span,QLineF(line.front(),p).length());
    const auto model_line=kernel::dimension_sub(angular?d.line_first:d.witness_second,d.witness_first);
    if(kernel::dimension_dot(model_line,model_line)>1e-18 && projected_span<=std::max(1e-7,scale*std::sqrt(kernel::dimension_dot(model_line,model_line))*1e-6))
        return out;
    const auto middle = angular ? line[line.size() / 2] : (a + b) * .5;
    auto along = dimension_screen_unit(
        angular ? line[line.size() / 2 + 1] - line[line.size() / 2 - 1] : b - a);
    auto requested = label_position ? project(*label_position) : middle;
    if (!std::isfinite(requested.x()) || !std::isfinite(requested.y()))
        return out;
    if (d.kind == kernel::ViewerDimensionKind::Radius) {
        // The measured rim and the text shelf have independent grips. All
        // three RMB modes use this same radius and its drawing plane.
        auto text_direction=out.oblique?QPointF(1,0):along;
        if(text_direction.x()<-1e-6||(std::abs(text_direction.x())<=1e-6&&text_direction.y()>0))text_direction=-text_direction;
        const double radius=QLineF(a,b).length();
        double position=dimension_screen_dot(requested-a,along);
        const bool beyond=position>=radius*.5;
        if(!d.radius_center_line_hidden)position=beyond?std::max(position,radius+arrow*1.7+text_width/2):std::min(position,-arrow*1.7-text_width/2);
        QPointF center,join;
        if(out.oblique){
            const double extension_side=position>=radius?1.:-1.;
            const double side=(along.x()<0?-1.:1.)*extension_side;
            const QPointF half_label(side*text_width/2,0);
            join=a+along*dimension_screen_dot(a+along*position-half_label-a,along);
            if(!d.radius_center_line_hidden){
                const double t=dimension_screen_dot(join-a,along);
                join=a+along*(beyond?std::max(t,radius+arrow*1.7):std::min(t,-arrow*1.7));
            }
            center=join+half_label;
        } else {
            center=a+along*position;
            const auto first=center-text_direction*text_width/2,last=center+text_direction*text_width/2;
            join=QLineF(b,first).length()<QLineF(b,last).length()?first:last;
        }
        const auto start=center-text_direction*text_width/2,end=center+text_direction*text_width/2;
        if(!d.radius_center_line_hidden)out.curves.push_back({a,b});
        out.curves.push_back({b,join});out.curves.push_back({start,end});
        const bool inside=dimension_screen_dot(join-b,along)<0;
        const auto arrow_direction=d.radius_center_line_hidden?(inside?along:-along):(d.arrows_reversed?along:-along);
        out.arrows={{b,arrow_direction}};
        if(!d.radius_center_line_hidden&&!d.arrows_reversed)out.curves.push_back({b,b+along*arrow*1.7});
        out.handles={center,b,b};out.outside=position<0||position>radius;
        out.text_baseline=start+QPointF(text_direction.y(),-text_direction.x())*gap;
        out.text_angle=std::atan2(text_direction.y(),text_direction.x())*180/std::numbers::pi;
        out.valid=true;return out;
    }
    double shift = dimension_screen_dot(requested - middle, along);
    if (std::abs(shift) < gap * 1.5)
        shift = 0;
    const double length = QLineF(a, b).length();
    out.outside = out.oblique || radial || std::abs(shift) + text_width / 2 + arrow > length / 2;
    auto text_direction = out.oblique ? QPointF(1, 0) : along;
    if (text_direction.x() < -1e-6 ||
        (std::abs(text_direction.x()) <= 1e-6 && text_direction.y() > 0))
        text_direction = -text_direction;
    QPointF center = middle + along * shift;
    QPointF attachment;
    if (out.outside) {
        if (out.oblique) {
            const double side = requested.x() < middle.x() - gap ? -1 : 1;
            attachment = (a.x() * side > b.x() * side) ? a : b;
            // The leader is an extension of the dimension, never a free diagonal
            // to the requested text position. An arc continues along its end tangent.
            const auto outward = dimension_screen_unit(
                attachment == a ? a - line[1] : b - line[line.size() - 2]);
            const auto half_label = QPointF(side * text_width / 2, 0);
            const double extension = std::max(
                arrow * 1.7, dimension_screen_dot(requested - half_label - attachment, outward));
            center = attachment + outward * extension + half_label;
        } else {
            const double side = shift < 0 ? -1 : 1;
            attachment = side < 0 ? a : b;
            center = middle + along * (side * std::max(std::abs(shift),
                                                       length / 2 + arrow * 1.7 + text_width / 2));
        }
    }
    const auto start = center - text_direction * text_width / 2,
               end = center + text_direction * text_width / 2;
    out.curves.push_back(line);
    if (out.outside) {
        const auto join =
            QLineF(attachment, start).length() < QLineF(attachment, end).length() ? start : end;
        out.curves.push_back({attachment, join});
    }
    out.curves.push_back({start, end});
    const bool external = out.outside != d.arrows_reversed;
    auto first_dir = dimension_screen_unit(line[1] - a),
         last_dir = dimension_screen_unit(b - line[line.size() - 2]);
    if (!radial || d.kind == kernel::ViewerDimensionKind::Diameter)
        out.arrows.push_back({a, external ? first_dir : -first_dir});
    out.arrows.push_back({b, external ? -last_dir : last_dir});
    if (external) {
        if (!radial || d.kind == kernel::ViewerDimensionKind::Diameter)
            out.curves.push_back({a, a - first_dir * arrow * 1.7});
        out.curves.push_back({b, b + last_dir * arrow * 1.7});
    }
    out.handles = {center, a, b};
    if (d.kind == kernel::ViewerDimensionKind::Radius)
        out.handles = {center, b, b};
    const QPointF up(text_direction.y(), -text_direction.x());
    out.text_baseline = start + up * gap;
    out.text_angle = std::atan2(text_direction.y(), text_direction.x()) * 180 / std::numbers::pi;
    out.valid = true;
    return out;
}
} // namespace zima::viewer
