#include <zima/sketcher/external_curve_center.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include "exact_external_circle.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace zima::sketcher {
namespace {
std::optional<std::array<double,2>> exact_ellipse_center(const kernel::BSplineGeometry& curve) {
    if(curve.degree!=2||curve.poles.size()<3||curve.weights.size()!=curve.poles.size()||curve.knots.size()!=curve.poles.size()+3)return {};
    if(std::ranges::any_of(curve.poles,[](const auto& p){return p.z!=0;}))return {};
    std::vector<double> breaks;
    for(double knot:curve.knots)if(breaks.empty()||knot!=breaks.back())breaks.push_back(knot);
    if(breaks.size()<2)return {};
    const double length=breaks.back()-breaks.front();if(!(length>0))return {};
    std::vector<kernel::BSplineGeometry> pieces;
    for(std::size_t i=1;i<breaks.size();++i) {
        kernel::BSplineGeometry piece;
        const bool chain=curve.poles.size()==2*(breaks.size()-1)+1&&
            std::ranges::count(curve.knots,breaks[i-1])==(i==1?3:2)&&
            std::ranges::count(curve.knots,breaks[i])==(i+1==breaks.size()?3:2);
        if(chain) {
            const auto at=2*(i-1);piece.degree=2;piece.knots={0,0,0,1,1,1};
            piece.poles.assign(curve.poles.begin()+at,curve.poles.begin()+at+3);
            piece.weights.assign(curve.weights.begin()+at,curve.weights.begin()+at+3);
        } else piece=trim_curve_geometry(curve,(breaks[i-1]-breaks.front())/length,(breaks[i]-breaks.front())/length);
        if(piece.poles.size()!=3||piece.weights.size()!=3)return {};
        pieces.push_back(std::move(piece));
    }
    // Build the first quadratic's implicit conic in normalized Sketch XY.
    // Barycentric coordinates satisfy (1-alpha-beta)^2 = 4*k*alpha*beta.
    const auto anchor=curve.poles.front();long double scale=0;
    for(const auto& p:curve.poles)scale=std::max(scale,std::hypot(static_cast<long double>(p.x)-anchor.x,static_cast<long double>(p.y)-anchor.y));
    if(!(scale>1e-12L))return {};
    using H=std::array<long double,3>;
    const auto& first=pieces.front();std::array<H,3> p;
    for(int i=0;i<3;++i)p[i]={(first.poles[i].x-anchor.x)/scale,(first.poles[i].y-anchor.y)/scale,1};
    const long double ux=p[0][0]-p[1][0],uy=p[0][1]-p[1][1],vx=p[2][0]-p[1][0],vy=p[2][1]-p[1][1];
    const long double determinant=ux*vy-uy*vx;
    if(std::abs(determinant)<1e-15L)return {};
    H a{vy/determinant,-vx/determinant,(-vy*p[1][0]+vx*p[1][1])/determinant};
    H b{-uy/determinant,ux/determinant,(uy*p[1][0]-ux*p[1][1])/determinant};
    H u{-a[0]-b[0],-a[1]-b[1],1-a[2]-b[2]};
    const long double k=static_cast<long double>(first.weights[1])*first.weights[1]/first.weights[0]/first.weights[2];
    if(!(k>0&&k<1))return {}; // Parabolas and hyperbolas have no ellipse center.
    std::array<H,3> q;long double norm=0;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){q[i][j]=u[i]*u[j]-2*k*(a[i]*b[j]+b[i]*a[j]);norm=std::max(norm,std::abs(q[i][j]));}
    if(!(norm>0))return {};
    for(auto& row:q)for(auto& value:row)value/=norm;
    const long double d=q[0][0]*q[1][1]-q[0][1]*q[0][1];
    if(d<=1e-16L)return {};
    const long double x=(q[0][1]*q[1][2]-q[1][1]*q[0][2])/d,y=(q[0][1]*q[0][2]-q[0][0]*q[1][2])/d;
    if(!std::isfinite(x)||!std::isfinite(y))return {};
    constexpr std::array<long double,3> choose2{1,2,1};
    constexpr std::array<long double,5> choose4{1,4,6,4,1};
    for(const auto& piece:pieces) {
        std::array<H,3> h;const double weights=*std::ranges::max_element(piece.weights);
        for(int i=0;i<3;++i){const long double w=piece.weights[i]/weights;h[i]={(piece.poles[i].x-anchor.x)/scale*w,(piece.poles[i].y-anchor.y)/scale*w,w};}
        std::array<long double,5> residual{};
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int r=0;r<3;++r)for(int c=0;c<3;++c)
            residual[i+j]+=choose2[i]*choose2[j]/choose4[i+j]*h[i][r]*q[r][c]*h[j][c];
        if(std::ranges::any_of(residual,[](auto value){return std::abs(value)>1e-11L;}))return {};
    }
    const std::array result{anchor.x+static_cast<double>(x*scale),anchor.y+static_cast<double>(y*scale)};
    if(!std::isfinite(result[0])||!std::isfinite(result[1]))return {};
    return result;
}
}
std::optional<std::array<double,2>> external_reference_center(const SketchExternalReference& reference) {
    if(reference.infinite||!reference.exact_spline||(reference.kind!=ExternalReferenceKind::Edge&&reference.kind!=ExternalReferenceKind::Face))return {};
    if(const auto circle=detail::exact_external_circle(*reference.exact_spline))return circle->center;
    return exact_ellipse_center(*reference.exact_spline);
}
}
