#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace zima::sketcher {
namespace {
using Curve = kernel::BSplineGeometry;
using V = kernel::Vec3;
using H = std::array<double, 4>;
V plus(V a,V b) {return {a.x+b.x,a.y+b.y,0};}
V minus(V a,V b) {return {a.x-b.x,a.y-b.y,0};}
V scale(V a,double s) {return {a.x*s,a.y*s,0};}
double length(V a) {return std::hypot(a.x,a.y);}
H homogeneous(const Curve& c,std::size_t i) {
    const auto p=c.poles[i];const auto w=c.weights[i];return {p.x*w,p.y*w,p.z*w,w};
}
void insert_knot(Curve& c,double u) {
    const auto p=c.degree, n=static_cast<unsigned>(c.poles.size()-1);
    const auto k=static_cast<unsigned>(std::upper_bound(c.knots.begin(),c.knots.end(),u)-c.knots.begin()-1);
    const auto s=static_cast<unsigned>(std::count(c.knots.begin(),c.knots.end(),u));
    std::vector<H> q(n+2);
    for(unsigned i=0;i<=k-p;++i)q[i]=homogeneous(c,i);
    for(unsigned i=k-s;i<=n;++i)q[i+1]=homogeneous(c,i);
    for(unsigned i=k-p+1;i<=k-s;++i) {
        const double a=(u-c.knots[i])/(c.knots[i+p]-c.knots[i]);
        const auto x=homogeneous(c,i-1),y=homogeneous(c,i);
        for(unsigned j=0;j<4;++j)q[i][j]=(1-a)*x[j]+a*y[j];
    }
    c.poles.clear();c.weights.clear();
    for(const auto& v:q){c.poles.push_back({v[0]/v[3],v[1]/v[3],v[2]/v[3]});c.weights.push_back(v[3]);}
    c.knots.insert(c.knots.begin()+k+1,u);
}
std::pair<Curve,Curve> split(Curve c,double fraction) {
    const double u=c.knots.front()+fraction*(c.knots.back()-c.knots.front());
    while(std::count(c.knots.begin(),c.knots.end(),u)<c.degree)insert_knot(c,u);
    const auto k=std::upper_bound(c.knots.begin(),c.knots.end(),u)-c.knots.begin()-1;
    const auto seam=k-c.degree;
    Curve a,b;a.degree=b.degree=c.degree;
    a.poles.assign(c.poles.begin(),c.poles.begin()+seam+1);
    a.weights.assign(c.weights.begin(),c.weights.begin()+seam+1);
    a.knots.assign(c.knots.begin(),c.knots.begin()+k+1);a.knots.push_back(u);
    b.poles.assign(c.poles.begin()+seam,c.poles.end());
    b.weights.assign(c.weights.begin()+seam,c.weights.end());
    b.knots={u};b.knots.insert(b.knots.end(),c.knots.begin()+seam+1,c.knots.end());
    a.validate();b.validate();return {a,b};
}
V tangent(const Curve& c,double t) {
    // Differentiate the homogeneous spline, then apply the rational quotient rule.
    const auto p=c.degree;const auto n=c.poles.size();
    const double range=c.knots[n]-c.knots[p],u=c.knots[p]+std::clamp(t,0.0,1.0)*range;
    const auto k=t>=1?n-1:static_cast<std::size_t>(std::upper_bound(c.knots.begin()+p,c.knots.begin()+n+1,u)-c.knots.begin()-1);
    std::vector<H> h(p+1),d(p);
    for(unsigned j=0;j<=p;++j)h[j]=homogeneous(c,k-p+j);
    for(unsigned j=0;j<p;++j) {
        const auto i=k-p+j;
        const double den=c.knots[i+p+1]-c.knots[i+1];
        for(unsigned a=0;a<4;++a)d[j][a]=den>0?p*(h[j+1][a]-h[j][a])/den:0;
    }
    for(unsigned r=1;r<=p;++r)for(unsigned j=p;j>=r;--j) {
        const auto i=k-p+j;const double den=c.knots[i+p-r+1]-c.knots[i];
        const double a=den>0?(u-c.knots[i])/den:0;
        for(unsigned q=0;q<4;++q)h[j][q]=(1-a)*h[j-1][q]+a*h[j][q];
    }
    for(unsigned r=1;r<p;++r)for(unsigned j=p-1;j>=r;--j) {
        const auto i=k-p+j+1;const double den=c.knots[i+p-r]-c.knots[i];
        const double a=den>0?(u-c.knots[i])/den:0;
        for(unsigned q=0;q<4;++q)d[j][q]=(1-a)*d[j-1][q]+a*d[j][q];
    }
    const auto a=h[p],b=d[p-1];
    return {range*(b[0]*a[3]-a[0]*b[3])/(a[3]*a[3]),range*(b[1]*a[3]-a[1]*b[3])/(a[3]*a[3]),0};
}
Curve conic(V center,double rx,double ry,double rotation,bool reverse,double start,double end) {
    if(!(end>start))throw std::invalid_argument("Empty curve interval");
    Curve c;c.degree=2;
    const auto count=static_cast<unsigned>(std::ceil((end-start)/(std::numbers::pi/2)));
    const auto point=[&](double a,double w) {const double x=rx*std::cos(a)/w,y=ry*std::sin(a)/w*(reverse?-1:1);
        return V{center.x+x*std::cos(rotation)-y*std::sin(rotation),center.y+x*std::sin(rotation)+y*std::cos(rotation),0};};
    c.knots={0,0,0};c.poles.push_back(point(start,1));c.weights.push_back(1);
    for(unsigned i=0;i<count;++i) {
        const double a=start+(end-start)*i/count,b=start+(end-start)*(i+1)/count,w=std::cos((b-a)/2);
        c.poles.push_back(point((a+b)/2,w));c.weights.push_back(w);
        c.poles.push_back(point(b,1));c.weights.push_back(1);
        const double t=double(i+1)/count;c.knots.insert(c.knots.end(),i+1==count?3:2,t);
    }
    c.validate();return c;
}
}

kernel::BSplineGeometry sketch_curve_geometry(const Sketch& s,const std::string& id) {
    const auto point=[&](const std::string& id){const auto* p=s.find_point(id);if(!p)throw std::invalid_argument("Missing curve point");return V{p->x,p->y,0};};
    for(const auto& c:s.segments)if(c.id==id){if(c.centerline)throw std::invalid_argument("Offset requires a finite curve");return {1,{point(c.first_point_id),point(c.second_point_id)},{0,0,1,1},{1,1}};}
    for(const auto& c:s.circles)if(c.id==id)return conic(point(c.center_point_id),c.radius,c.radius,0,false,0,2*std::numbers::pi);
    for(const auto& c:s.arcs)if(c.id==id)return conic(point(c.center_point_id),c.radius,c.radius,0,false,c.start_angle,c.end_angle);
    for(const auto& c:s.ellipses)if(c.id==id)return conic(point(c.center_point_id),c.major_radius,c.minor_radius,c.rotation,c.reversed,0,2*std::numbers::pi);
    for(const auto& c:s.elliptical_arcs)if(c.id==id)return conic(point(c.center_point_id),c.major_radius,c.minor_radius,c.rotation,c.reversed,c.start_parameter,c.end_parameter);
    for(const auto& c:s.bsplines)if(c.id==id) {
        Curve result;result.degree=c.degree;for(const auto& id:c.control_point_ids)result.poles.push_back(point(id));
        result.knots=c.knots;result.weights=c.weights;
        if(!c.knots.empty()){result.validate();return result;}
        const auto n=result.poles.size();
        if(c.interpolating) {
            const auto points=result.poles;result={};result.degree=3;result.knots={0,0,0,0};
            const auto count=c.closed?n:n-1;
            const auto at=[&](int i){return points[c.closed?(i+int(n))%int(n):std::clamp(i,0,int(n)-1)];};
            result.poles.push_back(at(0));
            for(unsigned i=0;i<count;++i) {
                const auto p0=at(int(i)-1),p1=at(i),p2=at(i+1),p3=at(i+2);
                result.poles.push_back(plus(p1,scale(minus(p2,p0),1.0/6)));
                result.poles.push_back(minus(p2,scale(minus(p3,p1),1.0/6)));
                result.poles.push_back(p2);result.knots.insert(result.knots.end(),i+1==count?4:3,double(i+1)/count);
            }
        } else if(c.closed) {
            // Uniform periodic source: clamp its full active interval by knot insertion.
            for(unsigned i=0;i<c.degree;++i)result.poles.push_back(result.poles[i]);
            for(unsigned i=0;i<result.poles.size()+c.degree+1;++i)result.knots.push_back(double(i));
            result.weights.assign(result.poles.size(),1);
            const double a=c.degree,b=n+c.degree;
            while(std::count(result.knots.begin(),result.knots.end(),a)<c.degree)insert_knot(result,a);
            while(std::count(result.knots.begin(),result.knots.end(),b)<c.degree)insert_knot(result,b);
            const auto first=std::upper_bound(result.knots.begin(),result.knots.end(),a)-result.knots.begin()-1-c.degree;
            const auto last=std::lower_bound(result.knots.begin(),result.knots.end(),b)-result.knots.begin()-1;
            const auto poles=result.poles;const auto knots=result.knots;
            result.poles.assign(poles.begin()+first,poles.begin()+last+1);
            result.knots.assign(c.degree+1,a);
            for(double k:knots)if(k>a&&k<b)result.knots.push_back(k);
            result.knots.insert(result.knots.end(),c.degree+1,b);
        } else {
            result.knots.assign(c.degree+1,0);
            for(unsigned i=1;i<n-c.degree;++i)result.knots.push_back(double(i)/(n-c.degree));
            result.knots.insert(result.knots.end(),c.degree+1,1);
        }
        result.weights.assign(result.poles.size(),1);result.validate();return result;
    }
    throw std::invalid_argument("Offset source must be an owned Sketch curve");
}

kernel::BSplineGeometry trim_curve_geometry(const Curve& c,double a,double b) {
    c.validate();if(!std::isfinite(a)||!std::isfinite(b)||a<0||b>1||b-a<1e-12)throw std::invalid_argument("Invalid retained curve interval");
    auto result=c;if(b<1)result=split(result,b).first;if(a>0)result=split(result,a/b).second;return result;
}

kernel::Vec3 curve_offset_point(const Curve& c,double t,double distance) {
    const auto p=kernel::bspline_value(c,t),v=tangent(c,t);const double speed=length(v);
    if(speed<1e-12)throw std::invalid_argument("Offset source has a singular tangent");
    return {p.x-distance*v.y/speed,p.y+distance*v.x/speed,0};
}

kernel::BSplineGeometry offset_curve_geometry(const Curve& c,double distance,double tolerance) {
    c.validate();if(!std::isfinite(distance)||!std::isfinite(tolerance)||tolerance<=0)throw std::invalid_argument("Invalid offset distance or tolerance");
    if(distance==0)return c;
    if(c.degree==1&&c.poles.size()==2){auto r=c;for(unsigned i=0;i<2;++i)r.poles[i]=curve_offset_point(c,i,distance);return r;}
    // Preserve circular arcs exactly instead of fitting an approximate spline.
    if(c.degree==2) {
        const auto a=kernel::bspline_value(c,0),b=kernel::bspline_value(c,.25),e=kernel::bspline_value(c,.5);
        const double bx=b.x-a.x,by=b.y-a.y,ex=e.x-a.x,ey=e.y-a.y,det=2*(bx*ey-by*ex);
        if(std::abs(det)>1e-14) {
            const double b2=bx*bx+by*by,e2=ex*ex+ey*ey;
            const V center{a.x+(b2*ey-e2*by)/det,a.y+(bx*e2-ex*b2)/det,0};
            const double radius=length(minus(a,center));bool circular=true;
            for(int i=1;i<=32;++i)if(std::abs(length(minus(kernel::bspline_value(c,i/32.),center))-radius)>std::max(1e-10,radius*1e-12)){circular=false;break;}
            if(circular) {
                const auto radial=minus(a,center),v=tangent(c,0);
                const double target=radius-distance*((radial.x*v.y-radial.y*v.x)>0?1:-1);
                if(target<=tolerance)throw std::invalid_argument("Offset collapses the circular curve");
                auto result=c;for(auto& p:result.poles)p=plus(center,scale(minus(p,center),target/radius));return result;
            }
        }
    }
    Curve result;result.degree=3;result.knots={0,0,0,0};
    const auto value=[&](double t){return curve_offset_point(c,t,distance);};
    const auto derivative=[&](double t,double a,double b) {const double h=std::min(1e-5,(b-a)*1e-3),lo=std::max(a,t-h),hi=std::min(b,t+h);return scale(minus(value(hi),value(lo)),1/(hi-lo));};
    std::function<void(double,double,unsigned)> append=[&](double a,double b,unsigned depth) {
        const auto p0=value(a),p3=value(b),d0=derivative(a,a,b),d1=derivative(b,a,b);
        const auto p1=plus(p0,scale(d0,(b-a)/3)),p2=minus(p3,scale(d1,(b-a)/3));
        double error=0;
        for(unsigned i=1;i<16;++i){const double u=double(i)/16,v=1-u;
            const auto q=plus(plus(scale(p0,v*v*v),scale(p1,3*v*v*u)),plus(scale(p2,3*v*u*u),scale(p3,u*u*u)));
            error=std::max(error,length(minus(q,value(a+(b-a)*u))));
        }
        if(error>tolerance*.25 || b-a>1.0/32) {
            if(depth>=20||result.poles.size()>30000)throw std::invalid_argument("Offset cannot meet the curve tolerance");
            append(a,(a+b)/2,depth+1);append((a+b)/2,b,depth+1);return;
        }
        // Reject a locally reversed branch (offset beyond its curvature radius).
        for(double t:{a+(b-a)*.25,(a+b)/2,a+(b-a)*.75}) {
            const auto ds=derivative(t,a,b),dt=tangent(c,t);
            if(ds.x*dt.x+ds.y*dt.y<=0)throw std::invalid_argument("Offset distance creates a cusp; reduce the distance or flip the side");
        }
        if(result.poles.empty())result.poles.push_back(p0);
        result.poles.insert(result.poles.end(),{p1,p2,p3});result.knots.insert(result.knots.end(),b==1?4:3,b);
    };
    std::vector<double> breaks{0};const double range=c.knots.back()-c.knots.front();
    for(double k:c.knots){const double t=(k-c.knots.front())/range;if(t>breaks.back()&&t<1)breaks.push_back(t);}breaks.push_back(1);
    for(std::size_t i=1;i<breaks.size();++i)append(breaks[i-1],breaks[i],0);
    result.weights.assign(result.poles.size(),1);result.validate();return result;
}
}
