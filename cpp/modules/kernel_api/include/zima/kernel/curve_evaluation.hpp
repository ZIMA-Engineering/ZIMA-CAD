#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <optional>

namespace zima::kernel {
// Native rational-curve evaluation, independent of OCCT and display samples.
inline Vec3 bspline_derivative(const BSplineGeometry& c, double t) {
    using H = std::array<double, 4>;
    const double range = c.knots[c.poles.size()] - c.knots[c.degree];
    const double u = c.knots[c.degree] + std::clamp(t, 0.0, 1.0) * range;
    const auto evaluate = [&](const std::vector<H>& poles,
                              const std::vector<double>& knots, unsigned degree) {
        const auto n = poles.size();
        const auto span = t >= 1 ? n - 1 : std::size_t(std::upper_bound(
            knots.begin() + degree, knots.begin() + n + 1, u) - knots.begin() - 1);
        std::vector<H> d(degree + 1);
        for (unsigned j = 0; j <= degree; ++j) d[j] = poles[span - degree + j];
        for (unsigned r = 1; r <= degree; ++r)
            for (unsigned j = degree; j >= r; --j) {
                const auto i = span - degree + j;
                const double den = knots[i + degree - r + 1] - knots[i];
                const double a = den > 0 ? (u - knots[i]) / den : 0;
                for (unsigned k = 0; k < 4; ++k) d[j][k] = (1-a)*d[j-1][k] + a*d[j][k];
            }
        return d[degree];
    };
    std::vector<H> poles;
    for (std::size_t i = 0; i < c.poles.size(); ++i) {
        const auto& p = c.poles[i]; const double w = c.weights[i];
        poles.push_back({p.x*w, p.y*w, p.z*w, w});
    }
    const auto h = evaluate(poles, c.knots, c.degree);
    std::vector<H> derivative(poles.size() - 1);
    for (std::size_t i = 0; i < derivative.size(); ++i) {
        const double den = c.knots[i+c.degree+1] - c.knots[i+1];
        for (unsigned k = 0; k < 4; ++k)
            derivative[i][k] = den > 0 ? c.degree*(poles[i+1][k]-poles[i][k])/den : 0;
    }
    const auto d = evaluate(derivative,
        std::vector<double>(c.knots.begin()+1, c.knots.end()-1), c.degree-1);
    return {(d[0]*h[3]-h[0]*d[3])*range/(h[3]*h[3]),
            (d[1]*h[3]-h[1]*d[3])*range/(h[3]*h[3]),
            (d[2]*h[3]-h[2]*d[3])*range/(h[3]*h[3])};
}

struct CurveProjection {
    double parameter{};
    Vec3 point;
    Vec3 tangent;
    double squared_distance{};
    bool unique_tangent{true};
};

inline CurveProjection project_bspline(const BSplineGeometry& c, const Vec3& p,
                                      std::optional<double> hint = {}) {
    c.validate();
    const auto distance = [&](double t) {
        const auto q = bspline_value(c, t);
        return (q.x-p.x)*(q.x-p.x)+(q.y-p.y)*(q.y-p.y)+(q.z-p.z)*(q.z-p.z);
    };
    double best = std::clamp(hint.value_or(0),0.,1.), error = distance(best);
    std::vector<double> coincident_parameters;
    const auto offer = [&](double t) {
        const double e = distance(t);
        if (e < 1e-14) coincident_parameters.push_back(t);
        const double tie = 1e-20 + 1e-13 * std::max(error, e);
        if (e < error - tie || (std::abs(e-error) <= tie && hint &&
            std::abs(t-*hint) < std::abs(best-*hint))) { best=t; error=e; }
    };
    offer(0); offer(1);
    const double low = c.knots[c.degree], range = c.knots[c.poles.size()]-low;
    // Bracket independently within every nonempty knot span, including corners.
    for (std::size_t k = c.degree; k < c.poles.size(); ++k) {
        if (c.knots[k+1] <= c.knots[k]) continue;
        const double a = (c.knots[k]-low)/range, b = (c.knots[k+1]-low)/range;
        if (c.degree == 1) {
            const auto first=bspline_value(c,a),last=bspline_value(c,b);
            const Vec3 d{last.x-first.x,last.y-first.y,last.z-first.z};
            const double square=d.x*d.x+d.y*d.y+d.z*d.z;
            const double fraction=square>0?std::clamp(((p.x-first.x)*d.x+(p.y-first.y)*d.y+(p.z-first.z)*d.z)/square,0.,1.):0;
            offer(a+(b-a)*fraction);continue;
        }
        const unsigned pieces = std::max(8u, c.degree*4);
        for (unsigned i = 0; i < pieces; ++i) {
            double left=a+(b-a)*i/pieces, right=a+(b-a)*(i+1)/pieces;
            offer(left); offer(right);
            constexpr double ratio=.6180339887498948482;
            double x=right-ratio*(right-left), y=left+ratio*(right-left);
            double fx=distance(x), fy=distance(y);
            for (unsigned n=0; n<60; ++n) {
                if (fx<fy) {right=y;y=x;fy=fx;x=right-ratio*(right-left);fx=distance(x);}
                else {left=x;x=y;fx=fy;y=left+ratio*(right-left);fy=distance(y);}
            }
            offer((left+right)*.5);
        }
    }
    const auto tangent=bspline_derivative(c,best);
    const double magnitude=std::hypot(tangent.x,tangent.y,tangent.z);
    bool unique=magnitude>1e-12;
    const auto same_direction=[&](double t) {
        const auto d=bspline_derivative(c,t);const double m=std::hypot(d.x,d.y,d.z);
        return m>1e-12 && (d.x*tangent.x+d.y*tangent.y+d.z*tangent.z)/(m*magnitude)>1-1e-6;
    };
    // A corner, cusp or self-intersection has no single tangent. Do not
    // silently choose an arbitrary branch to orient a dependent container.
    if(unique && error<1e-12) {
        for(const double t:coincident_parameters)if(std::abs(t-best)>1e-6 && !same_direction(t))unique=false;
        for(std::size_t k=c.degree+1;unique && k<c.poles.size();++k) {
            const double t=(c.knots[k]-low)/range;
            if(t>0 && t<1 && std::abs(t-best)<1e-7)
                unique=same_direction(t-1e-8)&&same_direction(t+1e-8);
        }
    }
    return {best, bspline_value(c,best), tangent, error, unique};
}
} // namespace zima::kernel
