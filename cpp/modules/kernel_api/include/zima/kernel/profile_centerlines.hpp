#pragma once
#include <zima/kernel/dimension_layout.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <numbers>
#include <functional>

namespace zima::kernel::profile_centerlines {
using E=ExtrusionRequest;
using Moments=std::array<double,3>;
inline Moments plus(Moments a,Moments b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
inline Moments times(Moments a,double v){for(auto& x:a)x*=v;return a;}
struct Frame {
    Vec3 origin,x,y;
    explicit Frame(const ProfileCenterlines& p):origin(p.origin) {
        const auto n=dimension_unit(p.normal);
        x=dimension_unit(dimension_cross(std::abs(n.x)<.8?Vec3{1,0,0}:Vec3{0,1,0},n));y=dimension_cross(n,x);
    }
    Vec3 local(Vec3 p)const{p=dimension_sub(p,origin);return {dimension_dot(p,x),dimension_dot(p,y),0};}
    Vec3 vector(Vec3 p)const{return {dimension_dot(p,x),dimension_dot(p,y),0};}
    Vec3 world(Vec3 p)const{return dimension_add(origin,dimension_add(dimension_scale(x,p.x),dimension_scale(y,p.y)));}
};
// Green's theorem: signed area and first moments from the exact native boundary.
inline Moments integrand(Vec3 p,Vec3 d){return {.5*(p.x*d.y-p.y*d.x),.5*p.x*p.x*d.y,-.5*p.y*p.y*d.x};}
inline Moments integrate(const std::function<Moments(double)>& f,double a,double b,int depth=0) {
    const auto gauss=[&](double lo,double hi) {
        constexpr double nodes[]{.1834346424956498,.5255324099163290,.7966664774136267,.9602898564975363};
        constexpr double weights[]{.3626837833783620,.3137066458778873,.2223810344533745,.1012285362903763};
        const double mid=(lo+hi)/2,half=(hi-lo)/2;Moments sum{};
        for(int i=0;i<4;++i)sum=plus(sum,times(plus(f(mid-half*nodes[i]),f(mid+half*nodes[i])),half*weights[i]));return sum;
    };
    const auto whole=gauss(a,b),split=plus(gauss(a,(a+b)/2),gauss((a+b)/2,b));
    bool converged=true;for(int i=0;i<3;++i)if(std::abs(split[i]-whole[i])>1e-10*std::max(1.,std::abs(split[i])))converged=false;
    if(converged)return split;
    if(depth>=16)throw std::runtime_error("Profile centroid integration did not converge.");
    return plus(integrate(f,a,(a+b)/2,depth+1),integrate(f,(a+b)/2,b,depth+1));
}
inline Moments segment(Vec3 a,Vec3 b){const double cross=a.x*b.y-b.x*a.y;return {cross/2,(b.y-a.y)*(a.x*a.x+a.x*b.x+b.x*b.x)/6,-(b.x-a.x)*(a.y*a.y+a.y*b.y+b.y*b.y)/6};}
inline Moments loop(const E::ProfileLoop& input,const Frame& frame) {
    auto moments=std::visit([&](const auto& profile)->Moments {
        using T=std::decay_t<decltype(profile)>;
        if constexpr(std::is_same_v<T,E::CircleProfile>||std::is_same_v<T,E::EllipseProfile>) {
            const auto c=frame.local(profile.center);double area;
            if constexpr(std::is_same_v<T,E::CircleProfile>)area=std::numbers::pi*profile.radius*profile.radius;
            else area=std::numbers::pi*profile.major_radius*profile.minor_radius;
            return {area,area*c.x,area*c.y};
        }else if constexpr(std::is_same_v<T,E::PolygonProfile>) {
            Moments sum{};for(std::size_t i=0;i<profile.vertices.size();++i)sum=plus(sum,segment(frame.local(profile.vertices[i]),frame.local(profile.vertices[(i+1)%profile.vertices.size()])));return sum;
        }else {
            Moments sum{};
            for(const auto& item:profile.curves)sum=plus(sum,std::visit([&](const auto& curve)->Moments {
                using C=std::decay_t<decltype(curve)>;
                if constexpr(std::is_same_v<C,E::LineCurve>)return segment(frame.local(curve.start),frame.local(curve.end));
                else if constexpr(std::is_same_v<C,E::BSplineCurve>) {
                    BSplineGeometry native;native.degree=curve.degree;native.poles=curve.control_points;native.knots=curve.knots;native.weights=curve.weights;
                    if(curve.interpolating)throw std::runtime_error("Profile centroid requires the resolved native spline.");
                    native.validate();Moments result{};const double first=native.knots[native.degree],last=native.knots[native.poles.size()];
                    for(std::size_t i=native.degree;i<native.poles.size();++i)if(native.knots[i+1]>native.knots[i])
                        result=plus(result,integrate([&](double t){return integrand(frame.local(bspline_value(native,t)),frame.vector(bspline_derivative(native,t)));},(native.knots[i]-first)/(last-first),(native.knots[i+1]-first)/(last-first)));
                    return result;
                }else {
                    Vec3 c,u,v;double begin,end;
                    if constexpr(std::is_same_v<C,E::ArcCurve>) {
                        const auto a=frame.local(curve.start),m=frame.local(curve.middle),b=frame.local(curve.end);
                        const auto am=dimension_sub(m,a),ab=dimension_sub(b,a);const double det=2*(am.x*ab.y-am.y*ab.x);
                        if(std::abs(det)<1e-16)throw std::runtime_error("Profile centroid has a degenerate arc.");
                        const double mm=dimension_dot(am,am),bb=dimension_dot(ab,ab);
                        c={a.x+(mm*ab.y-bb*am.y)/det,a.y+(am.x*bb-ab.x*mm)/det,0};
                        const double radius=std::hypot(a.x-c.x,a.y-c.y);u={radius,0,0};v={0,radius,0};
                        begin=std::atan2(a.y-c.y,a.x-c.x);
                        const auto positive=[](double x){x=std::fmod(x,2*std::numbers::pi);return x<0?x+2*std::numbers::pi:x;};
                        double sweep=positive(std::atan2(b.y-c.y,b.x-c.x)-begin);
                        if(positive(std::atan2(m.y-c.y,m.x-c.x)-begin)>sweep)sweep-=2*std::numbers::pi;
                        end=begin+sweep;
                    }else {
                        c=frame.local(curve.center);u=dimension_scale(frame.vector(dimension_unit(curve.major_axis_direction)),curve.major_radius);
                        const auto normal=dimension_cross(frame.x,frame.y);
                        v=dimension_scale(frame.vector(dimension_cross(normal,dimension_unit(curve.major_axis_direction))),curve.minor_radius);
                        begin=curve.start_parameter;end=curve.end_parameter;
                        if(curve.reversed){while(end>=begin)end-=2*std::numbers::pi;}else while(end<=begin)end+=2*std::numbers::pi;
                    }
                    return integrate([&](double t){const auto p=dimension_add(c,dimension_add(dimension_scale(u,std::cos(t)),dimension_scale(v,std::sin(t))));const auto d=dimension_add(dimension_scale(u,-std::sin(t)),dimension_scale(v,std::cos(t)));return integrand(p,d);},begin,end);
                }
            },item));return sum;
        }
    },input);
    return moments[0]<0?times(moments,-1):moments;
}
template<class Request> inline Vec3 centroid(const Request& request) {
    if(!request.open_profile_end_id.empty())throw std::runtime_error("Profile centroid requires a closed profile with positive area.");
    const Frame frame(request.centerlines);
    const auto region=[&](const auto& outer,const auto& holes){auto sum=loop(outer,frame);for(const auto& hole:holes)sum=plus(sum,times(loop(hole,frame),-1));return sum;};
    auto sum=region(request.outer_profile,request.inner_profiles);
    for(const auto& item:request.additional_profile_regions)sum=plus(sum,region(item.outer_profile,item.inner_profiles));
    if(!std::isfinite(sum[0])||sum[0]<=1e-12)throw std::runtime_error("Profile centroid requires a closed profile with positive area.");
    return frame.world({sum[1]/sum[0],sum[2]/sum[0],0});
}
template<class Request> inline std::vector<std::pair<std::string,Vec3>> seeds(const Request& request) {
    std::vector<std::pair<std::string,Vec3>> result;
    if(request.centerlines.origin_enabled)result.push_back({"centerline:from:origin:"+request.centerlines.origin_id,request.centerlines.origin});
    if(request.centerlines.centroid_enabled)result.push_back({"centerline:from:centroid:"+request.centerlines.profile_id,centroid(request)});
    return result;
}

inline void endpoints(ViewerReferenceGeometry& out,const std::string& owner,const std::string& key,Vec3 a,Vec3 b) {
    for(bool start:{true,false}){ViewerPoint p;p.position=start?a:b;p.reference={owner,"profile:path-point:"+std::string(start?"start:from:":"end:from:")+key,{}};p.display_owner_id=owner;out.points.push_back(p);}
}
inline void line(ViewerReferenceGeometry& out,const std::string& owner,const std::string& key,Vec3 a,Vec3 b) {
    const auto delta=dimension_sub(b,a);const double length=std::sqrt(dimension_dot(delta,delta));
    if(length<1e-12)return;
    out.axes.push_back({dimension_scale(dimension_add(a,b),.5),dimension_scale(delta,1/length),length+2.,{owner,key,{}}});
    endpoints(out,owner,key,a,b);
}
// A disabled Feature side has no swept body, but its authored path references
// still exist at the source profile. This is reference geometry only: never
// manufacture an epsilon-length OCCT solid to retain a downstream attachment.
template<class Request> inline ViewerReferenceGeometry stationary(
        const Request& request,const std::string& owner) {
    ViewerReferenceGeometry out;
    for(const auto& [key,seed]:seeds(request)) {
        Vec3 direction;
        if constexpr(std::is_same_v<Request,RevolutionRequest>) {
            const auto rotation_axis=dimension_unit(request.axis_direction);
            const auto offset=dimension_sub(seed,request.axis_point);
            direction=dimension_cross(rotation_axis,offset);
            if(dimension_dot(direction,direction)<1e-24)
                direction=request.profile_normal;
        } else {
            direction=request.direction;
        }
        if(!request.first_cap_is_start)direction=dimension_scale(direction,-1);
        direction=dimension_unit(direction);
        out.axes.push_back({seed,direction,2.,{owner,key,{}}});
        endpoints(out,owner,key,seed,seed);
    }
    return out;
}
inline ViewerReferenceGeometry revolution(const RevolutionRequest& request,const std::string& owner) {
    ViewerReferenceGeometry out;const auto normal=dimension_unit(request.axis_direction);
    const bool closed=std::abs(request.angle_degrees-360)<1e-9;
    for(const auto& [key,seed]:seeds(request)) {
        const auto delta=dimension_sub(seed,request.axis_point);
        const auto center=dimension_add(request.axis_point,dimension_scale(normal,dimension_dot(delta,normal)));
        const auto u=dimension_sub(seed,center);const double radius=std::sqrt(dimension_dot(u,u));
        // A seed on the rotation axis has coincident endpoints, not a new
        // "center" identity. Keep Start and End independently addressable.
        if(radius<1e-12){if(!closed)endpoints(out,owner,key,center,center);continue;}
        const auto v=dimension_cross(normal,u);
        const auto value=[&](double t){return dimension_add(center,dimension_add(dimension_scale(u,std::cos(t)),dimension_scale(v,std::sin(t))));};
        const double begin=request.start_angle_degrees*std::numbers::pi/180,end=begin+request.angle_degrees*std::numbers::pi/180;
        ViewerEdge edge;edge.reference={owner,key,{}};edge.display_owner_id=owner;edge.construction=edge.overlay=edge.dash_dot=true;
        edge.measured_length=radius*(end-begin);
        const int samples=std::max(2,int(std::ceil(request.angle_degrees/2)));
        for(int i=0;i<=samples;++i)edge.points.push_back(value(begin+(end-begin)*i/samples));
        // Exact rational quadratic geometry keeps downstream references independent of display tessellation.
        BSplineGeometry spline;spline.degree=2;const int pieces=std::max(1,int(std::ceil(request.angle_degrees/90)));
        spline.knots={0,0,0};spline.poles.push_back(value(begin));spline.weights.push_back(1);
        for(int i=0;i<pieces;++i){const double a=begin+(end-begin)*i/pieces,b=begin+(end-begin)*(i+1)/pieces,w=std::cos((b-a)/2);
            spline.poles.push_back(dimension_add(center,dimension_scale(dimension_sub(value((a+b)/2),center),1/w)));spline.weights.push_back(w);
            spline.poles.push_back(value(b));spline.weights.push_back(1);
            const double knot=double(i+1)/pieces;spline.knots.push_back(knot);spline.knots.push_back(knot);
        }
        spline.knots.push_back(1);spline.validate();edge.exact_spline=std::move(spline);out.edges.push_back(std::move(edge));
        if(!closed)endpoints(out,owner,key,value(request.first_cap_is_start?begin:end),value(request.first_cap_is_start?end:begin));
    }
    return out;
}
} // namespace zima::kernel::profile_centerlines
