#pragma once
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace zima::sketcher::detail {
struct ExactExternalCircle {
    std::array<double,2> center, first, last;
    double radius{}, sweep{};
    bool closed{};
};

// Certify a circular rational quadratic by its complete homogeneous polynomial,
// not its display samples. Nonquadratic/noncircular curves retain exact NURBS.
inline std::optional<ExactExternalCircle> exact_external_circle(const kernel::BSplineGeometry& curve) {
    if(curve.degree!=2||curve.poles.size()<3||curve.weights.size()!=curve.poles.size()||
       curve.knots.size()!=curve.poles.size()+3)return {};
    if(std::ranges::any_of(curve.poles,[](const auto& p){return p.z!=0;}))return {};
    const double low=curve.knots.front(),span=curve.knots.back()-low;
    if(!(span>0))return {};
    std::vector<double> breaks;
    for(double knot:curve.knots)if(breaks.empty()||knot!=breaks.back())breaks.push_back(knot);
    std::vector<kernel::BSplineGeometry> pieces;
    for(std::size_t i=1;i<breaks.size();++i) {
        kernel::BSplineGeometry piece;
        const bool bezier_chain=curve.poles.size()==2*(breaks.size()-1)+1&&
            std::ranges::count(curve.knots,breaks[i-1])==(i==1?3:2)&&
            std::ranges::count(curve.knots,breaks[i])==(i+1==breaks.size()?3:2);
        if(bezier_chain) {
            const auto offset=2*(i-1);piece.degree=2;piece.knots={0,0,0,1,1,1};
            piece.poles.assign(curve.poles.begin()+offset,curve.poles.begin()+offset+3);
            piece.weights.assign(curve.weights.begin()+offset,curve.weights.begin()+offset+3);
        } else piece=trim_curve_geometry(curve,(breaks[i-1]-low)/span,(breaks[i]-low)/span);
        if(piece.degree!=2||piece.poles.size()!=3)return {};
        pieces.push_back(std::move(piece));
    }
    if(pieces.empty())return {};
    const auto a=pieces.front().poles.front(),b=kernel::bspline_value(pieces.front(),.5),c=pieces.front().poles.back();
    const long double bx=b.x-a.x,by=b.y-a.y,cx=c.x-a.x,cy=c.y-a.y;
    const long double determinant=2*(bx*cy-by*cx),scale=std::max(std::hypot(bx,by),std::hypot(cx,cy));
    if(scale<=1e-12L||std::abs(determinant)<=scale*scale*1e-11L)return {};
    const long double ux=((bx*bx+by*by)*cy-(cx*cx+cy*cy)*by)/determinant;
    const long double uy=(bx*(cx*cx+cy*cy)-cx*(bx*bx+by*by))/determinant;
    ExactExternalCircle result;result.center={a.x+static_cast<double>(ux),a.y+static_cast<double>(uy)};
    result.radius=static_cast<double>(std::hypot(ux,uy));
    if(!std::isfinite(result.radius)||result.radius<=1e-12)return {};
    const long double tolerance=std::max(1e-11L,1e-10L/result.radius);
    constexpr std::array<long double,3> choose2{1,2,1};
    constexpr std::array<long double,5> choose4{1,4,6,4,1};
    int direction{};
    for(const auto& piece:pieces) {
        std::array<std::array<long double,3>,3> h;
        const double weight_scale=*std::ranges::max_element(piece.weights);
        for(int i=0;i<3;++i) {
            const long double w=piece.weights[i]/weight_scale;
            h[i]={(piece.poles[i].x-result.center[0])/result.radius*w,
                  (piece.poles[i].y-result.center[1])/result.radius*w,w};
        }
        std::array<long double,5> equation{};
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)
            equation[i+j]+=choose2[i]*choose2[j]/choose4[i+j]*(h[i][0]*h[j][0]+h[i][1]*h[j][1]-h[i][2]*h[j][2]);
        if(std::ranges::any_of(equation,[&](auto value){return std::abs(value)>tolerance;}))return {};
        const auto first=piece.poles.front(),middle=kernel::bspline_value(piece,.5),last=piece.poles.back();
        for(const auto& pair:{std::pair{first,middle},std::pair{middle,last}}) {
            const double ax=pair.first.x-result.center[0],ay=pair.first.y-result.center[1];
            const double bx=pair.second.x-result.center[0],by=pair.second.y-result.center[1];
            const double angle=std::atan2(ax*by-ay*bx,ax*bx+ay*by);
            if(std::abs(angle)<=1e-12)return {};
            const int sign=angle<0?-1:1;if(direction&&direction!=sign)return {};direction=sign;result.sweep+=angle;
        }
    }
    constexpr double turn=2*std::numbers::pi;
    if(std::abs(result.sweep)>turn+1e-9)return {};
    result.first={curve.poles.front().x,curve.poles.front().y};result.last={curve.poles.back().x,curve.poles.back().y};
    result.closed=std::abs(std::abs(result.sweep)-turn)<1e-9&&
        std::hypot(result.first[0]-result.last[0],result.first[1]-result.last[1])<=1e-10*std::max(1.,result.radius);
    return result;
}
} // namespace zima::sketcher::detail
