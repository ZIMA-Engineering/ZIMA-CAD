#include <zima/kernel/curve_plane_intersection.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Precision.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <limits>

namespace zima::kernel {
namespace {
using Polynomial=std::vector<long double>;
long double value(Polynomial controls,long double t) {
    for(std::size_t n=controls.size();n>1;--n)
        for(std::size_t i=0;i+1<n;++i)controls[i]=(1-t)*controls[i]+t*controls[i+1];
    return controls.front();
}
// A rational curve has positive weights, so its signed plane-distance zeros
// are exactly the roots of its homogeneous Bernstein numerator. Derivative
// roots partition that polynomial into monotone intervals and expose isolated
// even-multiplicity contacts which GeomAPI_IntCS can omit.
std::vector<long double> roots(Polynomial controls) {
    long double scale{};for(const auto coefficient:controls)scale=std::max(scale,std::abs(coefficient));
    if(scale==0||controls.size()<2)return {};
    for(auto& coefficient:controls)coefficient/=scale;
    const auto degree=controls.size()-1;Polynomial derivative(degree);
    for(std::size_t i=0;i<degree;++i)derivative[i]=degree*(controls[i+1]-controls[i]);
    auto partitions=roots(std::move(derivative));partitions.insert(partitions.begin(),0);partitions.push_back(1);
    const auto epsilon=128*std::numeric_limits<long double>::epsilon();
    std::vector<long double> result;
    for(const auto t:partitions)if(std::abs(value(controls,t))<=epsilon)result.push_back(t);
    for(std::size_t i=1;i<partitions.size();++i) {
        auto low=partitions[i-1],high=partitions[i];auto a=value(controls,low);const auto b=value(controls,high);
        if(std::abs(a)<=epsilon||std::abs(b)<=epsilon||std::signbit(a)==std::signbit(b))continue;
        for(unsigned step=0;step<96;++step) {
            const auto middle=(low+high)/2;if(middle==low||middle==high)break;
            const auto v=value(controls,middle);
            if(v==0){low=high=middle;break;}
            if(std::signbit(v)==std::signbit(a)){low=middle;a=v;}else high=middle;
        }
        result.push_back((low+high)/2);
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
CurvePlaneIntersectionResult intersect_curve_plane(const CurvePlaneIntersectionRequest& request) {
    constexpr auto error="Curve-plane intersection calculation failed.";
    const auto finite=[](Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    const double length=std::hypot(request.normal.x,request.normal.y,request.normal.z);
    if(!finite(request.origin)||!finite(request.normal)||!std::isfinite(length)||length<1e-12||
            !std::isfinite(request.tolerance)||request.tolerance<=0)throw std::invalid_argument(error);
    try {
        const auto curve=restore_bspline_geometry(request.curve);
        const gp_Dir normal(request.normal.x,request.normal.y,request.normal.z);
        const gp_Pln plane(gp_Pnt(request.origin.x,request.origin.y,request.origin.z),normal);
        const auto signed_distance=[&](const gp_Pnt& point){return gp_Vec(plane.Location(),point).Dot(gp_Vec(normal));};
        CurvePlaneIntersectionResult result;result.first_parameter=curve->FirstParameter();result.last_parameter=curve->LastParameter();
        const double parameter_tolerance=std::min(Precision::PConfusion(),(result.last_parameter-result.first_parameter)*1e-10);
        const double geometric_tolerance=std::min(request.tolerance,Precision::Confusion());
        if(request.closed&&curve->Value(result.first_parameter).Distance(curve->Value(result.last_parameter))>geometric_tolerance)
            throw std::invalid_argument(error);
        // Certify coincident native knot spans from their exact rational poles.
        // Viewer samples cannot distinguish a true interval from a tangency.
        GeomConvert_BSplineCurveToBezierCurve spans(curve);
        TColStd_Array1OfReal knots(1,spans.NbArcs()+1);spans.Knots(knots);
        std::vector<double> parameters;
        for(int i=1;i<=spans.NbArcs();++i) {
            const auto span=spans.Arc(i);bool coincident=true;long double max_weight{};
            for(int pole=1;pole<=span->NbPoles();++pole)max_weight=std::max(max_weight,static_cast<long double>(span->Weight(pole)));
            Polynomial numerator;
            for(int pole=1;pole<=span->NbPoles();++pole) {
                const auto distance=signed_distance(span->Pole(pole));
                coincident&=std::abs(distance)<=geometric_tolerance;
                const auto weight=span->Weight(pole)/max_weight;
                if(!std::isfinite(distance)||weight<=0)throw std::runtime_error(error);
                numerator.push_back(weight*distance);
            }
            if(coincident){result.coincident_intervals.emplace_back(knots.Value(i),knots.Value(i+1));continue;}
            for(const auto t:roots(std::move(numerator)))
                parameters.push_back(knots.Value(i)+static_cast<double>(t)*(knots.Value(i+1)-knots.Value(i)));
        }
        std::ranges::sort(result.coincident_intervals);
        std::vector<std::pair<double,double>> intervals;
        for(const auto& interval:result.coincident_intervals) {
            if(!intervals.empty()&&interval.first<=intervals.back().second+parameter_tolerance)
                intervals.back().second=std::max(intervals.back().second,interval.second);
            else intervals.push_back(interval);
        }
        result.coincident_intervals=std::move(intervals);
        const auto offer=[&](double parameter) {
            if(!std::isfinite(parameter)||parameter<result.first_parameter-parameter_tolerance||parameter>result.last_parameter+parameter_tolerance)
                throw std::runtime_error(error);
            parameter=std::clamp(parameter,result.first_parameter,result.last_parameter);
            if(std::abs(parameter-result.first_parameter)<=parameter_tolerance)parameter=result.first_parameter;
            if(std::abs(parameter-result.last_parameter)<=parameter_tolerance)parameter=result.last_parameter;
            if(std::ranges::any_of(result.coincident_intervals,[&](const auto& interval){
                return parameter>=interval.first-parameter_tolerance&&parameter<=interval.second+parameter_tolerance;}))return;
            const auto point=curve->Value(parameter);
            if(std::abs(signed_distance(point))>request.tolerance)throw std::runtime_error(error);
            // Merge only the same native parameter, never equal XYZ positions
            // reached by different branches of a self-intersecting curve.
            if(std::ranges::any_of(result.points,[&](const auto& contact){return std::abs(parameter-contact.parameter)<=parameter_tolerance;}))return;
            result.points.push_back({parameter,{point.X(),point.Y(),point.Z()}});
        };
        for(const auto parameter:parameters)offer(parameter);
        for(double parameter:{result.first_parameter,result.last_parameter})
            if(std::abs(signed_distance(curve->Value(parameter)))<=geometric_tolerance)offer(parameter);
        std::ranges::sort(result.points,{},&CurvePlaneContact::parameter);
        if(request.closed&&result.points.size()>1&&
                std::abs(result.points.front().parameter-result.first_parameter)<=parameter_tolerance&&
                std::abs(result.points.back().parameter-result.last_parameter)<=parameter_tolerance)result.points.pop_back();
        for(std::size_t i=0;i<result.points.size();++i) {
            auto& point=result.points[i];
            if(!request.closed&&(std::abs(point.parameter-result.first_parameter)<=parameter_tolerance||
                    std::abs(point.parameter-result.last_parameter)<=parameter_tolerance)) {point.kind=CurvePlaneContactKind::Endpoint;continue;}
            const double period=result.last_parameter-result.first_parameter;
            double left=i?result.points[i-1].parameter:request.closed?result.points.back().parameter-period:result.first_parameter;
            double right=i+1<result.points.size()?result.points[i+1].parameter:request.closed?result.points.front().parameter+period:result.last_parameter;
            for(const auto& interval:result.coincident_intervals)
                for(double shift:request.closed?std::vector<double>{-period,0,period}:std::vector<double>{0}) {
                    const auto first=interval.first+shift,last=interval.second+shift;
                    if(last<point.parameter&&last>left)left=last;
                    if(first>point.parameter&&first<right)right=first;
                }
            const auto probe=[&](double parameter) {
                if(parameter<result.first_parameter)parameter+=period;
                if(parameter>result.last_parameter)parameter-=period;
                return signed_distance(curve->Value(parameter));
            };
            const double a=probe((left+point.parameter)*.5),b=probe((right+point.parameter)*.5);
            if(a==0||b==0)throw std::runtime_error(error);
            point.kind=std::signbit(a)!=std::signbit(b)?CurvePlaneContactKind::Crossing:CurvePlaneContactKind::Tangent;
            point.orientation=(point.kind==CurvePlaneContactKind::Crossing?b:a)<0?-1:1;
        }
        return result;
    }catch(const Standard_Failure&){throw std::runtime_error(error);}
}
} // namespace zima::kernel
