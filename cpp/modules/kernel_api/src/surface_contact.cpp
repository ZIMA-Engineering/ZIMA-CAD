#include <zima/kernel/exact_surface.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <limits>
#include <numeric>

namespace zima::kernel {
namespace {
constexpr double residual_tolerance=1e-7;
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 scale(Vec3 a,double k){return {a.x*k,a.y*k,a.z*k};}
double square(Vec3 a){return dot(a,a);}
using Plane=std::pair<Vec3,double>;
using H=std::array<double,4>;

// Exact knot insertion exposes the polynomial numerator of each rational trim
// span. Convex-hull subdivision isolates crossings and even-multiplicity roots
// without flattening the trimming curve into display segments.
struct BezierSpan {double first,last;std::vector<H> poles;};
std::vector<BezierSpan> bezier_spans(const BSplineGeometry& curve) {
    auto knots=curve.knots;std::vector<H> poles;
    for(std::size_t i=0;i<curve.poles.size();++i) {
        const auto p=curve.poles[i];const double w=curve.weights[i];poles.push_back({p.x*w,p.y*w,p.z*w,w});
    }
    std::vector<double> interior;
    for(std::size_t i=curve.degree+1;i<curve.poles.size();++i)
        if(interior.empty()||interior.back()!=knots[i])interior.push_back(knots[i]);
    for(double u:interior)while(unsigned(std::count(knots.begin(),knots.end(),u))<curve.degree) {
        const auto k=std::size_t(std::upper_bound(knots.begin(),knots.end(),u)-knots.begin()-1);
        const auto multiplicity=std::size_t(std::count(knots.begin(),knots.end(),u));
        std::vector<H> next(poles.size()+1);
        for(std::size_t i=0;i<=k-curve.degree;++i)next[i]=poles[i];
        for(std::size_t i=k-multiplicity;i<poles.size();++i)next[i+1]=poles[i];
        for(std::size_t i=k-curve.degree+1;i<=k-multiplicity;++i) {
            const double a=(u-knots[i])/(knots[i+curve.degree]-knots[i]);
            for(unsigned c=0;c<4;++c)next[i][c]=(1-a)*poles[i-1][c]+a*poles[i][c];
        }
        poles=std::move(next);knots.insert(knots.begin()+k+1,u);
    }
    std::vector<BezierSpan> result;
    const double low=curve.knots[curve.degree],range=curve.knots[curve.poles.size()]-low;
    for(std::size_t k=curve.degree;k<poles.size();++k)if(knots[k+1]>knots[k])
        result.push_back({(knots[k]-low)/range,(knots[k+1]-low)/range,
            {poles.begin()+k-curve.degree,poles.begin()+k+1}});
    return result;
}
std::vector<double> trim_crossings(const BSplineGeometry& curve,double y) {
    std::vector<double> result;
    const auto isolate=[&](const auto& self,std::vector<double> values,double a,double b,unsigned depth)->void {
        const auto [low,high]=std::minmax_element(values.begin(),values.end());
        const double epsilon=1e-14*std::max({1.,std::abs(*low),std::abs(*high)});
        if(*low>epsilon||*high<-epsilon)return;
        if(*high-*low<=epsilon&&std::abs(*low)<=epsilon)return; // Coincident spans are checked by distance.
        if(depth==0||b-a<1e-12){result.push_back((a+b)*.5);return;}
        std::vector<double> left{values.front()},right{values.back()};
        for(std::size_t n=values.size()-1;n>0;--n) {
            for(std::size_t i=0;i<n;++i)values[i]=(values[i]+values[i+1])*.5;
            left.push_back(values[0]);right.push_back(values[n-1]);
        }
        std::reverse(right.begin(),right.end());const double m=(a+b)*.5;
        self(self,std::move(left),a,m,depth-1);self(self,std::move(right),m,b,depth-1);
    };
    for(const auto& span:bezier_spans(curve)) {
        std::vector<double> values;for(const auto& p:span.poles)values.push_back(p[1]-y*p[3]);
        isolate(isolate,std::move(values),span.first,span.last,48);
    }
    for(double t:{0.,1.})if(std::abs(bspline_value(curve,t).y-y)<1e-12)result.push_back(t);
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(),result.end(),[](double a,double b){return std::abs(a-b)<1e-8;}),result.end());
    return result;
}
bool solve_matrix(std::array<std::array<double,4>,3>& a,unsigned count) {
    for(unsigned c=0;c<count;++c) {
        unsigned pivot=c;for(unsigned i=c+1;i<count;++i)if(std::abs(a[i][c])>std::abs(a[pivot][c]))pivot=i;
        if(std::abs(a[pivot][c])<1e-24)return false;
        std::swap(a[c],a[pivot]);const auto divisor=a[c][c];
        for(unsigned j=c;j<=count;++j)a[c][j]/=divisor;
        for(unsigned i=0;i<count;++i)if(i!=c){const auto k=a[i][c];for(unsigned j=c;j<=count;++j)a[i][j]-=k*a[c][j];}
    }
    return true;
}
std::optional<std::vector<Plane>> independent_planes(const std::vector<Plane>& planes) {
    std::vector<Plane> result;
    for(const auto& [raw,rhs]:planes) {
        auto n=raw;double b=rhs;
        const double magnitude=std::sqrt(square(n));if(!std::isfinite(magnitude)||magnitude<1e-15||!std::isfinite(b))return {};
        n=scale(n,1/magnitude);b/=magnitude;
        for(const auto& [normal,value]:result) {const auto k=dot(n,normal);n=sub(n,scale(normal,k));b-=k*value;}
        const double remaining=std::sqrt(square(n));
        if(remaining>1e-10)result.emplace_back(scale(n,1/remaining),b/remaining);
        else if(std::abs(b)>residual_tolerance)return {};
    }
    return result;
}
std::vector<double> seeds(double lo,double hi,const std::vector<double>& knots,unsigned subdivisions=4) {
    std::vector<double> result{lo,hi};
    for(double k:knots)if(k>lo&&k<hi)result.push_back(k);
    std::ranges::sort(result);result.erase(std::unique(result.begin(),result.end()),result.end());
    const auto bounds=result;for(std::size_t i=1;i<bounds.size();++i)
        for(unsigned j=1;j<subdivisions;++j)result.push_back(std::lerp(bounds[i-1],bounds[i],double(j)/subdivisions));
    std::ranges::sort(result);return result;
}
bool translation_parameter(const ExactSurface& s,bool along_u) {
    if(s.kind==ExactSurface::Kind::Extrusion)return !along_u;
    if(s.kind==ExactSurface::Kind::Plane)return true;
    if(s.kind!=ExactSurface::Kind::Spline)return false;
    const auto count=along_u?s.u_count:s.v_count,other=along_u?s.v_count:s.u_count;
    const auto index=[&](unsigned i,unsigned j){return along_u?i*s.v_count+j:j*s.v_count+i;};
    Vec3 direction{};
    for(unsigned i=1;i<count;++i) {
        const auto d=sub(s.poles[index(i,0)],s.poles[index(0,0)]);
        if(square(d)>square(direction))direction=d;
    }
    if(square(direction)<1e-24)return false;
    for(unsigned i=0;i<count;++i) {
        const auto d=sub(s.poles[index(i,0)],s.poles[index(0,0)]);
        if(square(sub(d,scale(direction,dot(d,direction)/square(direction))))>1e-24*std::max(1.,square(d)))return false;
        for(unsigned j=0;j<other;++j) {
            if(s.weights[index(i,j)]!=s.weights[index(0,j)])return false;
            if(square(sub(sub(s.poles[index(i,j)],s.poles[index(0,j)]),d))>1e-24*std::max(1.,square(d)))return false;
        }
    }
    return true;
}
}

bool exact_surface_domain_contains(const ExactSurfaceGeometry& g,double u,double v) {
    if(u<g.u_min-1e-10||u>g.u_max+1e-10||v<g.v_min-1e-10||v>g.v_max+1e-10)return false;
    if(g.trim_loops.empty())return true;
    int winding=0;
    for(const auto& loop:g.trim_loops)for(std::size_t i=0;i<loop.size();++i) {
        const auto& edge=loop[i];
        if(project_bspline(edge,{u,v,0}).squared_distance<=1e-18)return true;
        for(double t:trim_crossings(edge,v)) {
            if(t>=1.-1e-9)continue; // The next curve owns the shared endpoint.
            const auto p=bspline_value(edge,t);if(p.x<=u)continue;
            constexpr double h=1e-7;
            const double before=t<h?bspline_value(loop[(i+loop.size()-1)%loop.size()],1-h).y:bspline_value(edge,t-h).y;
            const double after=bspline_value(edge,std::min(1.,t+h)).y;
            if(before<=v&&after>v)++winding;
            if(before>v&&after<=v)--winding;
        }
    }
    return winding!=0;
}

SurfaceContactSolutions solve_exact_surface_contact(const ExactSurfaceGeometry& g,Vec3 seed,double offset,
        const std::vector<Plane>& input,bool alternatives) {
    SurfaceContactSolutions result;
    const auto reduced=independent_planes(input);if(!reduced||!g.support)return result;
    const auto& planes=*reduced;result.discrete=planes.size()>=2;
    const auto* basis=g.support.get();while(basis->kind==ExactSurface::Kind::Offset)basis=basis->basis.get();
    const auto error=[&](const SurfaceEvaluation& p) {
        double sum=0;for(const auto& [n,b]:planes){const double r=dot(n,p.point)-b;sum+=r*r;}
        return sum;
    };
    const auto offer=[&](double u,double v,bool boundary=false) {
        const auto p=evaluate_exact_surface(g,u,v,offset);
        if(!p.regular||error(p)>residual_tolerance*residual_tolerance||(!boundary&&!exact_surface_domain_contains(g,u,v)))return;
        if(std::ranges::none_of(result.contacts,[&](const auto& c){return square(sub(c.point,p.point))<1e-14;}))
            result.contacts.push_back({u,v,p.point,p.normal});
    };
    // A line contained in a spline patch is a continuous contact, not a list
    // of arbitrary grid points. At fixed U/V, each plane equation has a
    // polynomial numerator of the corresponding degree. Degree+1 distinct
    // zeros on every knot span establish an identically zero numerator.
    const auto continuous_isoline=[&](double u,double v,bool along_u) {
        if(planes.size()!=2)return false;
        // A translational parameter has a constant normal along its ruling.
        // Offsetting therefore retains the rational polynomial in that
        // parameter. Establish this from the control net, not sampled normals.
        if(!(offset==0&&g.support->kind==ExactSurface::Kind::Spline)&&
                !translation_parameter(*basis,along_u))return false;
        const auto& knots=along_u?basis->u_knots:basis->v_knots;
        const auto degree=along_u?basis->u_degree:basis->v_degree;
        const auto samples=seeds(along_u?g.u_min:g.v_min,along_u?g.u_max:g.v_max,knots,degree+1);
        for(double t:samples) {
            const auto p=exact_surface_point(g,along_u?t:u,along_u?v:t,offset);
            if(!p)return false;
            for(const auto& [n,b]:planes)
                if(std::abs(dot(n,*p)-b)>1e-11*std::max({1.,std::abs(b),std::sqrt(square(*p))}))return false;
        }
        result.discrete=false;
        const auto objective=[&](double t) {
            const auto p=exact_surface_point(g,along_u?t:u,along_u?v:t,offset);
            return p?square(sub(*p,seed)):std::numeric_limits<double>::infinity();
        };
        for(std::size_t i=1;i<samples.size();++i) {
            double a=samples[i-1],b=samples[i];
            for(unsigned iteration=0;iteration<64;++iteration) {
                const double x=std::lerp(a,b,.38196601125010515),y=std::lerp(a,b,.61803398874989485);
                if(objective(x)<objective(y))b=y;else a=x;
            }
            const double t=(a+b)*.5;offer(along_u?t:u,along_u?v:t);
        }
        return true;
    };
    const auto solve=[&](double u,double v) {
        double damping=1e-10;
        for(unsigned iteration=0;iteration<80;++iteration) {
            const auto p=evaluate_exact_surface(g,u,v,offset);if(!p.regular)return;
            const auto delta=sub(p.point,seed);
            std::array<std::array<double,4>,3> a{};unsigned n=2;
            double merit=0,weight=1;
            if(planes.size()>=2) {
                for(const auto& [normal,rhs]:planes) {
                    const double r=dot(normal,p.point)-rhs,du=dot(normal,p.du),dv=dot(normal,p.dv);
                    a[0][0]+=du*du;a[0][1]+=du*dv;a[1][1]+=dv*dv;
                    a[0][2]-=du*r;a[1][2]-=dv*r;merit+=r*r;
                }
                a[1][0]=a[0][1];
                if(merit<1e-22){
                    if(continuous_isoline(u,v,true)||continuous_isoline(u,v,false))return;
                    // A tangent root has quadratic residual: a tiny plane
                    // residual alone does not establish an accurate position.
                    // Continue Newton refinement until the geometric step is
                    // small, or the equations are satisfied to roundoff.
                    if(merit<1e-32){offer(u,v);return;}
                }
            } else {
                a[0][0]=dot(p.du,p.du);a[0][1]=a[1][0]=dot(p.du,p.dv);a[1][1]=dot(p.dv,p.dv);
                const double gu=dot(delta,p.du),gv=dot(delta,p.dv);merit=square(delta);
                if(planes.empty()) {
                    a[0][2]=-gu;a[1][2]=-gv;
                    if(std::hypot(gu,gv)<1e-10){offer(u,v);return;}
                } else {
                    n=3;const auto [normal,rhs]=planes.front();
                    const double h=dot(normal,p.point)-rhs,hu=dot(normal,p.du),hv=dot(normal,p.dv);
                    a[0][2]=a[2][0]=hu;a[1][2]=a[2][1]=hv;
                    a[0][3]=-gu;a[1][3]=-gv;a[2][3]=-h;
                    const double tangent_gradient=gu*hv-gv*hu;
                    if(std::abs(h)<1e-9&&std::abs(tangent_gradient)<1e-9){offer(u,v);return;}
                    weight=std::max(1.,4.*std::hypot(gu,gv)/std::max(1e-12,std::hypot(hu,hv)));
                    merit+=weight*std::abs(h);
                }
            }
            a[0][0]+=damping;a[1][1]+=damping;
            if(!solve_matrix(a,n)){damping=std::max(1e-8,damping*10);continue;}
            if(planes.size()<2) {
                const Vec3 tangent_step{p.du.x*a[0][n]+p.dv.x*a[1][n],
                    p.du.y*a[0][n]+p.dv.y*a[1][n],p.du.z*a[0][n]+p.dv.z*a[1][n]};
                // Near a nearest-point solution, subtracting two almost equal
                // squared distances can round to zero before the raw parameter
                // gradient does. Test the physical tangent correction instead;
                // rejecting that converged interior contact would leave only
                // distant finite-boundary candidates. offer() still verifies
                // the independent equations and the exact trimmed domain.
                if(square(tangent_step)<1e-16) {
                    // Take the final small correction before accepting. With
                    // an on-surface seed, returning the preceding iterate
                    // causes the same tangential drift on every resolution.
                    offer(std::clamp(u+a[0][n],g.u_min,g.u_max),
                        std::clamp(v+a[1][n],g.v_min,g.v_max));
                    return;
                }
            }
            bool accepted=false;
            for(double step=1.;step>1e-7;step*=.5) {
                const double next_u=std::clamp(u+step*a[0][n],g.u_min,g.u_max),next_v=std::clamp(v+step*a[1][n],g.v_min,g.v_max);
                const auto q=evaluate_exact_surface(g,next_u,next_v,offset);if(!q.regular)continue;
                double next=planes.size()>=2?error(q):square(sub(q.point,seed));
                if(planes.size()==1)next+=weight*std::abs(dot(planes.front().first,q.point)-planes.front().second);
                if(next<merit) {
                    if(std::abs(next_u-u)+std::abs(next_v-v)<1e-12){offer(next_u,next_v);return;}
                    u=next_u;v=next_v;accepted=true;damping=std::max(1e-24,damping*.1);break;
                }
            }
            if(!accepted)return;
        }
    };
    const auto us=seeds(g.u_min,g.u_max,basis->u_knots,std::max(4u,2*basis->u_degree+1)),
        vs=seeds(g.v_min,g.v_max,basis->v_knots,std::max(4u,2*basis->v_degree+1));
    std::vector<std::pair<double,std::pair<double,double>>> starts;
    for(double u:us)for(double v:vs) {
        const auto p=exact_surface_point(g,u,v,offset);if(p)starts.push_back({square(sub(*p,seed)),{u,v}});
    }
    std::ranges::sort(starts);
    for(const auto& [distance,uv]:starts) {
        solve(uv.first,uv.second);
        // A contact already at the seed is an exact fixed point. Preserve it
        // without repeating global searches during regeneration.
        if(!alternatives&&!result.contacts.empty()&&square(sub(result.contacts.back().point,seed))<1e-18) {
            const auto contact=result.contacts.back();result.contacts={contact};return result;
        }
    }
    // Include finite trimming boundaries. These are exact UV curves evaluated
    // on the same mathematical surface; no display polyline is consulted.
    auto loops=g.trim_loops;
    if(loops.empty()) {
        const std::array<Vec3,5> corners{{{g.u_min,g.v_min,0},{g.u_max,g.v_min,0},{g.u_max,g.v_max,0},{g.u_min,g.v_max,0},{g.u_min,g.v_min,0}}};
        loops.resize(1);for(unsigned i=0;i<4;++i)loops.front().push_back({1,{corners[i],corners[i+1]},{0,0,1,1},{1,1}});
    }
    for(const auto& loop:loops)for(const auto& edge:loop) {
        const auto on_curve=[&](double t){const auto uv=bspline_value(edge,t);return exact_surface_point(g,uv.x,uv.y,offset);};
        const auto objective=[&](double t) {
            const auto p=on_curve(t);if(!p)return std::numeric_limits<double>::infinity();
            if(planes.empty())return square(sub(*p,seed));
            double sum=0;for(const auto& [n,b]:planes){const double r=dot(n,*p)-b;sum+=r*r;}return sum;
        };
        const auto accept=[&](double t){const auto uv=bspline_value(edge,t);offer(uv.x,uv.y,true);};
        for(const auto& span:bezier_spans(edge))for(unsigned segment=0;segment<std::max(8u,edge.degree*4);++segment) {
            const auto count=std::max(8u,edge.degree*4);
            double a=std::lerp(span.first,span.last,double(segment)/count),b=std::lerp(span.first,span.last,double(segment+1)/count);
            accept(a);accept(b);
            constexpr double ratio=.6180339887498948482;
            double x=b-ratio*(b-a),y=a+ratio*(b-a),fx=objective(x),fy=objective(y);
            for(unsigned iteration=0;iteration<48;++iteration) {
                if(fx<fy){b=y;y=x;fy=fx;x=b-ratio*(b-a);fx=objective(x);}
                else {a=x;x=y;fx=fy;y=a+ratio*(b-a);fy=objective(y);}
            }
            accept((a+b)*.5);
        }
    }
    std::ranges::sort(result.contacts,[&](const auto& a,const auto& b) {
        const auto da=square(sub(a.point,seed)),db=square(sub(b.point,seed));
        return da!=db?da<db:std::tie(a.u,a.v)<std::tie(b.u,b.v);
    });
    if(!alternatives&&!result.contacts.empty())result.contacts.resize(1);
    return result;
}
}
