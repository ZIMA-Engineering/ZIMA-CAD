#include <zima/kernel/exact_surface.hpp>
#include <functional>
#include <numbers>

namespace zima::kernel {
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
double dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}

// Truncated bivariate Taylor coefficients. Differentiating an offset consumes
// one order of its basis. Recursive evaluation requests exactly the necessary
// order, so all returned derivatives also remain exact for nested offsets.
class Jet {
public:
    unsigned order;
    std::vector<double> coefficients;
    explicit Jet(unsigned n,double value=0):order(n),coefficients((n+1)*(n+1)) {at(0,0)=value;}
    double& at(unsigned u,unsigned v) {return coefficients[u*(order+1)+v];}
    double at(unsigned u,unsigned v) const {return u+v<=order?coefficients[u*(order+1)+v]:0.;}
    double value() const {return at(0,0);}
    Jet truncated(unsigned n) const {
        Jet result(n);
        for(unsigned i=0;i<=n;++i)for(unsigned j=0;j+i<=n;++j)result.at(i,j)=at(i,j);
        return result;
    }
    Jet derivative(bool u) const {
        Jet result(order-1);
        for(unsigned i=0;i<order;++i)for(unsigned j=0;j+i<order;++j)
            result.at(i,j)=u?(i+1)*at(i+1,j):(j+1)*at(i,j+1);
        return result;
    }
};
Jet operator+(Jet a,const Jet& b) {
    for(std::size_t i=0;i<a.coefficients.size();++i)a.coefficients[i]+=b.coefficients[i];
    return a;
}
Jet operator-(Jet a,const Jet& b) {
    for(std::size_t i=0;i<a.coefficients.size();++i)a.coefficients[i]-=b.coefficients[i];
    return a;
}
Jet operator*(Jet a,double b) {for(auto& x:a.coefficients)x*=b;return a;}
Jet operator*(const Jet& a,const Jet& b) {
    Jet result(a.order);
    for(unsigned i=0;i<=a.order;++i)for(unsigned j=0;j+i<=a.order;++j)
        for(unsigned k=0;k<=i;++k)for(unsigned l=0;l<=j;++l)
            result.at(i,j)+=a.at(k,l)*b.at(i-k,j-l);
    return result;
}
Jet power(const Jet& a,double exponent) {
    if(!(a.value()>0))throw std::domain_error("Singular exact surface evaluation");
    auto delta=a;delta.at(0,0)=0;delta=delta*(1./a.value());
    Jet result(a.order,1),term(a.order,1);
    double coefficient=1;
    for(unsigned k=1;k<=a.order;++k) {
        term=term*delta;coefficient*= (exponent-double(k-1))/double(k);
        result=result+term*coefficient;
    }
    return result*std::pow(a.value(),exponent);
}
Jet operator/(const Jet& a,const Jet& b) {
    if(b.value()<0)return (a*power(b*(-1.),-1.))*(-1.);
    return a*power(b,-1.);
}
Jet trig(const Jet& a,bool cosine) {
    auto delta=a;delta.at(0,0)=0;
    Jet result(a.order),term(a.order,1);
    double factorial=1;
    for(unsigned k=0;k<=a.order;++k) {
        if(k){factorial*=k;term=term*delta;}
        const unsigned phase=(k+(cosine?1:0))%4;
        const double value=phase==0?std::sin(a.value()):phase==1?std::cos(a.value()):
            phase==2?-std::sin(a.value()):-std::cos(a.value());
        result=result+term*(value/factorial);
    }
    return result;
}
Jet exponential(const Jet& a) {
    auto delta=a;delta.at(0,0)=0;Jet result(a.order,1),term(a.order,1);double factorial=1;
    for(unsigned k=1;k<=a.order;++k){term=term*delta;factorial*=k;result=result+term*(1./factorial);}
    return result*std::exp(a.value());
}
using JPoint=std::array<Jet,3>;
using HPoint=std::array<Jet,4>;
JPoint constant(Vec3 p,unsigned n) {return {Jet(n,p.x),Jet(n,p.y),Jet(n,p.z)};}
JPoint operator+(JPoint a,const JPoint& b) {for(unsigned i=0;i<3;++i)a[i]=a[i]+b[i];return a;}
JPoint operator-(JPoint a,const JPoint& b) {for(unsigned i=0;i<3;++i)a[i]=a[i]-b[i];return a;}
JPoint operator*(JPoint a,const Jet& b) {for(auto& x:a)x=x*b;return a;}
JPoint operator*(JPoint a,double b) {for(auto& x:a)x=x*b;return a;}
Jet jdot(const JPoint& a,const JPoint& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
JPoint jcross(const JPoint& a,const JPoint& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
JPoint derivative(const JPoint& p,bool u) {return {p[0].derivative(u),p[1].derivative(u),p[2].derivative(u)};}
JPoint truncated(const JPoint& p,unsigned n) {return {p[0].truncated(n),p[1].truncated(n),p[2].truncated(n)};}
JPoint normal(const JPoint& p) {
    const auto raw=jcross(derivative(p,true),derivative(p,false));
    return raw*power(jdot(raw,raw),-.5);
}
std::size_t span(const std::vector<double>& knots,unsigned degree,std::size_t count,double parameter) {
    if(parameter>=knots[count])return count-1;
    if(parameter<=knots[degree])return degree;
    return std::size_t(std::upper_bound(knots.begin()+degree,knots.begin()+count+1,parameter)-knots.begin()-1);
}
HPoint de_boor(std::vector<HPoint> d,const std::vector<double>& knots,unsigned degree,std::size_t k,const Jet& t) {
    for(unsigned r=1;r<=degree;++r)for(unsigned j=degree;j>=r;--j) {
        const auto i=k-degree+j;const double denominator=knots[i+degree-r+1]-knots[i];
        const auto a=denominator>0?(t-Jet(t.order,knots[i]))*(1./denominator):Jet(t.order);
        for(unsigned c=0;c<4;++c)d[j][c]=d[j-1][c]*(Jet(t.order,1)-a)+d[j][c]*a;
    }
    return d[degree];
}
JPoint rational(const HPoint& p) {return {p[0]/p[3],p[1]/p[3],p[2]/p[3]};}
JPoint curve(const BSplineGeometry& c,const Jet& t) {
    const auto k=span(c.knots,c.degree,c.poles.size(),t.value());
    std::vector<HPoint> d;d.reserve(c.degree+1);
    for(unsigned j=0;j<=c.degree;++j) {
        const auto i=k-c.degree+j;const auto& p=c.poles[i];const double w=c.weights[i];
        d.push_back({Jet(t.order,p.x*w),Jet(t.order,p.y*w),Jet(t.order,p.z*w),Jet(t.order,w)});
    }
    return rational(de_boor(std::move(d),c.knots,c.degree,k,t));
}
JPoint generator(const ExactSurfaceGenerator& c,double parameter,unsigned order,bool u) {
    Jet t(order,parameter);if(order)t.at(u?1:0,u?0:1)=1;
    const auto o=constant(c.origin,order),x=constant(c.x_axis,order),y=constant(c.y_axis,order);
    switch(c.kind) {
    case ExactSurfaceGenerator::Kind::Line:return o+x*t;
    case ExactSurfaceGenerator::Kind::Circle:return o+x*(trig(t,true)*c.radius)+y*(trig(t,false)*c.radius);
    case ExactSurfaceGenerator::Kind::Ellipse:return o+x*(trig(t,true)*c.radius)+y*(trig(t,false)*c.minor_radius);
    case ExactSurfaceGenerator::Kind::Parabola:return o+x*(t*t*(1./(4*c.radius)))+y*t;
    case ExactSurfaceGenerator::Kind::Hyperbola:
        return o+x*((exponential(t)+exponential(t*(-1.)))*(.5*c.radius))+
            y*((exponential(t)-exponential(t*(-1.)))*(.5*c.minor_radius));
    case ExactSurfaceGenerator::Kind::Spline:return curve(c.spline,t);
    case ExactSurfaceGenerator::Kind::Offset: {
        const auto p=generator(*c.basis,parameter,order+1,u);
        const auto n=jcross(derivative(p,u),constant(c.z_axis,order));
        return truncated(p,order)+n*(power(jdot(n,n),-.5)*c.offset);
    }
    }
    throw std::invalid_argument("Invalid exact surface generator");
}
JPoint spline(const ExactSurface& s,const Jet& u,const Jet& v) {
    const auto ku=span(s.u_knots,s.u_degree,s.u_count,u.value());
    const auto kv=span(s.v_knots,s.v_degree,s.v_count,v.value());
    std::vector<HPoint> along_v;along_v.reserve(s.v_degree+1);
    for(unsigned j=0;j<=s.v_degree;++j) {
        std::vector<HPoint> along_u;along_u.reserve(s.u_degree+1);
        for(unsigned i=0;i<=s.u_degree;++i) {
            const auto index=(ku-s.u_degree+i)*s.v_count+kv-s.v_degree+j;
            const auto& p=s.poles[index];const double w=s.weights[index];
            along_u.push_back({Jet(u.order,p.x*w),Jet(u.order,p.y*w),Jet(u.order,p.z*w),Jet(u.order,w)});
        }
        along_v.push_back(de_boor(std::move(along_u),s.u_knots,s.u_degree,ku,u));
    }
    return rational(de_boor(std::move(along_v),s.v_knots,s.v_degree,kv,v));
}
JPoint evaluate(const ExactSurface& s,double pu,double pv,unsigned order) {
    Jet u(order,pu),v(order,pv);if(order){u.at(1,0)=1;v.at(0,1)=1;}
    const auto o=constant(s.origin,order),x=constant(s.x_axis,order),
        y=constant(s.y_axis,order),z=constant(s.z_axis,order);
    const auto radial=[&]{return x*trig(u,true)+y*trig(u,false);};
    switch(s.kind) {
    case ExactSurface::Kind::Plane:return o+x*u+y*v;
    case ExactSurface::Kind::Cylinder:return o+radial()*s.radius+z*v;
    case ExactSurface::Kind::Cone:return o+radial()*(Jet(order,s.radius)+v*std::sin(s.semi_angle))+z*(v*std::cos(s.semi_angle));
    case ExactSurface::Kind::Sphere:return o+radial()*(trig(v,true)*s.radius)+z*(trig(v,false)*s.radius);
    case ExactSurface::Kind::Torus:return o+radial()*(Jet(order,s.radius)+trig(v,true)*s.minor_radius)+z*(trig(v,false)*s.minor_radius);
    case ExactSurface::Kind::Spline:return spline(s,u,v);
    case ExactSurface::Kind::Extrusion:return generator(*s.generator,pu,order,true)+z*v;
    case ExactSurface::Kind::Revolution: {
        const auto delta=generator(*s.generator,pv,order,false)-o;
        const auto axial=z*jdot(z,delta),r=delta-axial;
        return o+axial+r*trig(u,true)+jcross(z,r)*trig(u,false);
    }
    case ExactSurface::Kind::Offset: {
        const auto basis=evaluate(*s.basis,pu,pv,order+1);
        return truncated(basis,order)+normal(basis)*s.offset;
    }
    }
    throw std::invalid_argument("Invalid exact surface kind");
}
JPoint placed(const ExactSurfaceGeometry& s,JPoint p,unsigned order) {
    return constant(s.origin,order)+constant(s.x_axis,order)*p[0]+
        constant(s.y_axis,order)*p[1]+constant(s.z_axis,order)*p[2];
}
Vec3 coefficient(const JPoint& p,unsigned u,unsigned v,double factor=1) {
    return {p[0].at(u,v)*factor,p[1].at(u,v)*factor,p[2].at(u,v)*factor};
}
}

void ExactSurfaceGenerator::validate() const {
    if(kind<Kind::Line||kind>Kind::Offset||!finite(origin)||!finite(x_axis)||!finite(y_axis)||!finite(z_axis)||
            !std::isfinite(radius)||!std::isfinite(minor_radius)||!std::isfinite(offset))
        throw std::invalid_argument("Invalid exact surface generator");
    if(kind==Kind::Spline)spline.validate();
    if(kind==Kind::Offset) {
        const ExactSurfaceGenerator* node=this;std::set<const ExactSurfaceGenerator*> seen;
        while(node->kind==Kind::Offset){if(!node->basis||!seen.insert(node).second||
                !finite(node->z_axis)||!std::isfinite(node->offset))
                throw std::invalid_argument("Invalid exact surface generator");node=node->basis.get();}
        node->validate();
    }
}
void ExactSurface::validate() const {
    if(kind<Kind::Plane||kind>Kind::Offset||!finite(origin)||!finite(x_axis)||!finite(y_axis)||!finite(z_axis)||
            !std::isfinite(radius)||!std::isfinite(minor_radius)||!std::isfinite(semi_angle)||!std::isfinite(offset))
        throw std::invalid_argument("Invalid exact surface data");
    if(kind==Kind::Spline) {
        const auto valid_knots=[](unsigned degree,unsigned count,const auto& knots) {
            return degree>=1&&count>degree&&knots.size()==std::size_t(count)+degree+1&&
                std::ranges::is_sorted(knots)&&std::ranges::all_of(knots,[](double x){return std::isfinite(x);})&&
                knots[degree]<knots[count];
        };
        if(!valid_knots(u_degree,u_count,u_knots)||!valid_knots(v_degree,v_count,v_knots)||
                poles.size()!=std::size_t(u_count)*v_count||weights.size()!=poles.size()||
                !std::ranges::all_of(poles,finite)||
                !std::ranges::all_of(weights,[](double x){return std::isfinite(x)&&x>0;}))
            throw std::invalid_argument("Invalid exact surface spline arrays");
    }
    if(kind==Kind::Extrusion||kind==Kind::Revolution) {
        if(!generator)throw std::invalid_argument("Invalid exact surface generator");generator->validate();
    }
    if(kind==Kind::Offset) {
        const ExactSurface* node=this;
        std::set<const ExactSurface*> seen;
        while(node->kind==Kind::Offset) {
            if(!node->basis||!seen.insert(node).second||!std::isfinite(node->offset))
                throw std::invalid_argument("Invalid exact offset surface");
            node=node->basis.get();
        }
        node->validate();
    }
}
void ExactSurfaceGeometry::validate() const {
    const double scale=std::hypot(x_axis.x,x_axis.y,x_axis.z)*
        std::hypot(y_axis.x,y_axis.y,y_axis.z)*std::hypot(z_axis.x,z_axis.y,z_axis.z);
    if(!support||!finite(origin)||!finite(x_axis)||!finite(y_axis)||!finite(z_axis)||
            !std::isfinite(scale)||scale==0||std::abs(dot(x_axis,cross(y_axis,z_axis)))<=1e-14*scale||
            !std::isfinite(u_min)||!std::isfinite(u_max)||!std::isfinite(v_min)||!std::isfinite(v_max)||
            u_min>=u_max||v_min>=v_max)throw std::invalid_argument("Invalid exact surface frame or domain");
    support->validate();
    for(const auto& loop:trim_loops) {
        if(loop.empty())throw std::invalid_argument("Invalid exact surface trim loop");
        for(const auto& edge:loop)edge.validate();
    }
}
SurfaceEvaluation evaluate_exact_surface(const ExactSurfaceGeometry& s,double u,double v,double offset) {
    if(!s.support||!std::isfinite(u)||!std::isfinite(v)||!std::isfinite(offset))return {};
    try {
        const double orientation=(s.reversed?-1.:1.)*(dot(s.x_axis,cross(s.y_axis,s.z_axis))<0?-1.:1.);
        auto p=placed(s,evaluate(*s.support,u,v,offset!=0?3:2),offset!=0?3:2);
        const auto n=normal(p)*orientation;
        if(offset!=0) {
            p=truncated(p,2)+n*offset;
            // Reject a singular offset without replacing the authored side by
            // the possibly reversed parameterization of that offset.
            static_cast<void>(normal(p));
        }
        SurfaceEvaluation result{coefficient(p,0,0),coefficient(p,1,0),coefficient(p,0,1),
            coefficient(p,2,0,2),coefficient(p,1,1),coefficient(p,0,2,2),coefficient(n,0,0),true};
        if(!finite(result.point)||!finite(result.du)||!finite(result.dv)||!finite(result.duu)||
                !finite(result.duv)||!finite(result.dvv)||!finite(result.normal))return {};
        return result;
    } catch(const std::domain_error&) {return {};}
}
std::optional<Vec3> exact_surface_point(const ExactSurfaceGeometry& s,double u,double v,double offset) {
    if(!s.support||!std::isfinite(u)||!std::isfinite(v)||!std::isfinite(offset))return {};
    try {
        const auto order=offset!=0?1u:0u;
        auto p=placed(s,evaluate(*s.support,u,v,order),order);
        if(offset!=0) {
            const double orientation=(s.reversed?-1.:1.)*(dot(s.x_axis,cross(s.y_axis,s.z_axis))<0?-1.:1.);
            p=truncated(p,0)+normal(p)*(offset*orientation);
        }
        const auto result=coefficient(p,0,0);
        return finite(result)?std::optional{result}:std::nullopt;
    }catch(const std::domain_error&){return {};}
}

namespace {
template<class T> bool same_definition(const std::shared_ptr<const T>& a,const std::shared_ptr<const T>& b) {
    if(a==b)return true;
    if(!a||!b)return false;
    if(std::tie(a->kind,a->origin,a->x_axis,a->y_axis,a->z_axis,a->radius,a->minor_radius,a->offset)!=
       std::tie(b->kind,b->origin,b->x_axis,b->y_axis,b->z_axis,b->radius,b->minor_radius,b->offset))return false;
    if constexpr(std::is_same_v<T,ExactSurfaceGenerator>) {
        if(a->spline!=b->spline)return false;
    } else {
        if(std::tie(a->semi_angle,a->u_degree,a->v_degree,a->u_count,a->v_count,a->u_knots,a->v_knots,a->poles,a->weights)!=
           std::tie(b->semi_angle,b->u_degree,b->v_degree,b->u_count,b->v_count,b->u_knots,b->v_knots,b->poles,b->weights)||
           !same_definition(a->generator,b->generator))return false;
    }
    return same_definition(a->basis,b->basis);
}
}
bool same_exact_surface(const std::shared_ptr<const ExactSurfaceGeometry>& a,
                        const std::shared_ptr<const ExactSurfaceGeometry>& b) {
    if(a==b)return true;
    return a&&b&&
        std::tie(a->origin,a->x_axis,a->y_axis,a->z_axis,a->u_min,a->u_max,a->v_min,a->v_max,a->reversed,a->trim_loops)==
        std::tie(b->origin,b->x_axis,b->y_axis,b->z_axis,b->u_min,b->u_max,b->v_min,b->v_max,b->reversed,b->trim_loops)&&
        same_definition(a->support,b->support);
}
}
