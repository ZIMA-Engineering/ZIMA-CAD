#include <zima/sketcher/vector_profile.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <QCryptographicHash>
#include <QByteArray>
#include <algorithm>
#include <cmath>
namespace zima::sketcher {
void append_exact_profile(Sketch& target,const std::string& source,
    const std::vector<kernel::BSplineGeometry>& boundaries) {
    for(std::size_t i=0;i<boundaries.size();++i) {
        const auto& curve=boundaries[i];curve.validate();
        if(curve.degree!=3 || (curve.poles.size()-1)%3!=0)
            throw std::runtime_error("The vector profile is not closed.");
        const auto& first=curve.poles.front();const auto& last=curve.poles.back();
        if(std::hypot(first.x-last.x,first.y-last.y)>1e-8)
            throw std::runtime_error("The vector profile is not closed.");
        const auto spans=(curve.poles.size()-1)/3;
        const auto contour=source+":contour:"+std::to_string(i);
        // A sharp corner must be a topological vertex: a single C0 spline
        // edge gives a non-regular lateral face and unreliable Booleans.
        // Smooth consecutive Bezier spans remain one exact native curve.
        std::vector<std::size_t> breaks{0};
        for(std::size_t span=1;span<spans;++span) {
            const auto at=3*span;const auto& p=curve.poles[at];
            kernel::Vec3 incoming{},outgoing{};
            for(std::size_t k=1;k<=3;++k) {
                const auto& q=curve.poles[at-k];incoming={p.x-q.x,p.y-q.y,0};
                if(std::hypot(incoming.x,incoming.y)>1e-13)break;
            }
            for(std::size_t k=1;k<=3;++k) {
                const auto& q=curve.poles[at+k];outgoing={q.x-p.x,q.y-p.y,0};
                if(std::hypot(outgoing.x,outgoing.y)>1e-13)break;
            }
            const double size=std::hypot(incoming.x,incoming.y)*std::hypot(outgoing.x,outgoing.y);
            if(size<=1e-26 || incoming.x*outgoing.x+incoming.y*outgoing.y<=0 ||
               std::abs(incoming.x*outgoing.y-incoming.y*outgoing.x)>1e-10*size)
                breaks.push_back(span);
        }
        breaks.push_back(spans);
        const auto point_id=[&](std::size_t pole) {
            return contour+":pole:"+std::to_string(pole==curve.poles.size()-1?0:pole);
        };
        for(std::size_t j=0;j+1<curve.poles.size();++j) {
            SketchPoint point;point.id=point_id(j);point.x=curve.poles[j].x;point.y=curve.poles[j].y;
            target.points.push_back(std::move(point));
        }
        for(std::size_t piece=0;piece+1<breaks.size();++piece) {
            const auto begin=breaks[piece],end=breaks[piece+1];
            SketchBSpline spline;spline.id=contour+":span:"+std::to_string(begin);
            spline.degree=3;spline.closed=breaks.size()==2;
            for(std::size_t pole=3*begin;pole<=3*end;++pole) {
                spline.control_point_ids.push_back(point_id(pole));
                spline.weights.push_back(curve.weights.empty()?1.:curve.weights[pole]);
            }
            spline.knots.insert(spline.knots.end(),4,0.);
            for(std::size_t span=begin+1;span<end;++span)
                spline.knots.insert(spline.knots.end(),3,double(span-begin)/double(end-begin));
            spline.knots.insert(spline.knots.end(),4,1.);
            target.bsplines.push_back(std::move(spline));
        }
    }
}
bool has_vector_profiles(const Sketch& sketch) {
    return std::ranges::any_of(sketch.reference_images,[](const auto& image){return image.modeling_geometry;}) || std::ranges::any_of(sketch.texts,[](const auto& t){return t.modeling_geometry&&!t.embedded_font.empty();});
}
Sketch expanded_vector_profiles(const Sketch& sketch) {
    auto result=sketch;
    for(auto& text:result.texts)if(text.modeling_geometry&&!text.embedded_font.empty()) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArray::fromStdString(text.embedded_font.encoded()));
        hash.addData(QByteArray(1,'\0'));
        hash.addData(QByteArray::fromStdString(text.value));
        // Editing a glyph/font cannot silently rebind old edges to unrelated
        // glyph geometry. Placement and size changes retain the same source IDs.
        const auto source="text:"+text.id+":"+hash.result().toHex().toStdString();
        const auto curves=exact_text_curves(text);text.modeling_geometry=false;
        append_exact_profile(result,source,curves);
    }
    for(auto& image:result.reference_images)image.modeling_geometry=false;
    result.validate();return result;
}
}
