#include <zima/kernel/exact_surface.hpp>
#include <chrono>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <future>
#include <limits>

using namespace zima::kernel;
namespace {
void check(bool valid,const char* text) {if(!valid)throw std::runtime_error(text);}
void near(double actual,double expected,double tolerance=1e-9) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance)
        throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void near(Vec3 a,Vec3 b,double tolerance=1e-9) {near(a.x,b.x,tolerance);near(a.y,b.y,tolerance);near(a.z,b.z,tolerance);}
Vec3 normalized(Vec3 p) {const auto n=std::hypot(p.x,p.y,p.z);return {p.x/n,p.y/n,p.z/n};}
Vec3 sub(Vec3 a,Vec3 b,double factor=1) {return {(a.x-b.x)*factor,(a.y-b.y)*factor,(a.z-b.z)*factor};}
Vec3 scale(Vec3 a,double k) {return {a.x*k,a.y*k,a.z*k};}
ExactSurfaceGeometry bowl() {
    auto s=std::make_shared<ExactSurface>();s->kind=ExactSurface::Kind::Spline;
    s->u_degree=s->v_degree=2;s->u_count=s->v_count=3;
    s->u_knots=s->v_knots={0,0,0,1,1,1};
    for(unsigned u=0;u<3;++u)for(unsigned v=0;v<3;++v) {
        s->poles.push_back({u*.5,v*.5,(u==2?1.:0)+(v==2?2.:0)});s->weights.push_back(1);
    }
    ExactSurfaceGeometry g;g.support=s;g.validate();return g;
}
void derivatives(const ExactSurfaceGeometry& g,double u,double v,double offset=0) {
    const double h=1e-5;const auto p=evaluate_exact_surface(g,u,v,offset);
    const auto up=evaluate_exact_surface(g,u+h,v,offset),um=evaluate_exact_surface(g,u-h,v,offset);
    const auto vp=evaluate_exact_surface(g,u,v+h,offset),vm=evaluate_exact_surface(g,u,v-h,offset);
    check(p.regular&&up.regular&&um.regular&&vp.regular&&vm.regular,"Nonregular derivative fixture");
    near(p.du,sub(up.point,um.point,.5/h),2e-7);near(p.dv,sub(vp.point,vm.point,.5/h),2e-7);
    near(p.duu,sub(up.du,um.du,.5/h),2e-6);near(p.duv,sub(vp.du,vm.du,.5/h),2e-6);
    near(p.dvv,sub(vp.dv,vm.dv,.5/h),2e-6);
}
void polynomial_and_frames() {
    const auto g=bowl();
    for(double u:{0.,.13,.43,.87,1.})for(double v:{0.,.19,.72,1.}) {
        const auto p=evaluate_exact_surface(g,u,v);
        check(p.regular,"Bowl is not regular");near(p.point,{u,v,u*u+2*v*v});
        near(p.du,{1,0,2*u});near(p.dv,{0,1,4*v});
        near(p.duu,{0,0,2});near(p.duv,{});near(p.dvv,{0,0,4});
        near(p.normal,normalized({-2*u,-4*v,1}));
    }
    for(bool mirror:{false,true})for(bool reverse:{false,true})for(double scale:{.01,1.,10.}) {
        const auto point=[&](Vec3 p){return Vec3{11+scale*p.z,-7+scale*p.x,5+(mirror?-scale:scale)*p.y};};
        const auto vector=[&](Vec3 p){return Vec3{scale*p.z,scale*p.x,(mirror?-scale:scale)*p.y};};
        auto placed=transform_exact_surface(g,point,vector);placed.reversed=reverse;placed.validate();
        for(double offset:{0.,-0.,.001,-.001}) {
            const double u=.37,v=.61;const auto expected=point({u,v,u*u+2*v*v});
            auto n=normalized(vector(normalized({-2*u,-4*v,1})));
            if(reverse)n={-n.x,-n.y,-n.z};
            const auto p=evaluate_exact_surface(placed,u,v,offset);
            check(p.regular,"Placed bowl is not regular");
            near(p.point,{expected.x+offset*n.x,expected.y+offset*n.y,expected.z+offset*n.z});
            near(p.normal,n,1e-8);derivatives(placed,u,v,offset);
        }
    }
}
void rational_and_procedural() {
    auto s=std::make_shared<ExactSurface>();s->kind=ExactSurface::Kind::Spline;
    s->u_degree=2;s->u_count=3;s->v_degree=1;s->v_count=2;
    s->u_knots={0,0,0,1,1,1};s->v_knots={0,0,1,1};
    for(Vec3 p:std::array<Vec3,3>{{{1,0,0},{1,1,0},{0,1,0}}})for(int v:{0,1}) {
        s->poles.push_back({p.x,p.y,double(v)});s->weights.push_back(p.x==p.y?std::sqrt(.5):1.);
    }
    ExactSurfaceGeometry g;g.support=s;g.validate();
    for(double u:{.13,.43,.87})for(double offset:{-.2,0.,.3}) {
        const auto p=evaluate_exact_surface(g,u,.39,offset);
        near(p.point.x*p.point.x+p.point.y*p.point.y,(1+offset)*(1+offset));near(p.point.z,.39);
        near(p.normal,normalized({p.point.x,p.point.y,0}));derivatives(g,u,.39,offset);
    }
    for(auto kind:{ExactSurface::Kind::Plane,ExactSurface::Kind::Cylinder,ExactSurface::Kind::Cone,
            ExactSurface::Kind::Sphere,ExactSurface::Kind::Torus,ExactSurface::Kind::Extrusion,ExactSurface::Kind::Revolution}) {
        auto p=std::make_shared<ExactSurface>();p->kind=kind;p->radius=3;p->minor_radius=.7;p->semi_angle=.3;
        auto generator=std::make_shared<ExactSurfaceGenerator>();
        generator->spline={2,{{2,0,0},{2.5,0,0},{3,0,1}},{0,0,0,1,1,1},{1,1,1}};
        p->generator=generator;
        if(kind==ExactSurface::Kind::Extrusion)p->z_axis={0,1,0};
        g.support=p;g.validate();
        for(double offset:{-.1,0.,.2})derivatives(g,.31,.57,offset);
        if(kind==ExactSurface::Kind::Extrusion)near(evaluate_exact_surface(g,.31,.57).point,{2.31,.57,.31*.31});
        if(kind==ExactSurface::Kind::Revolution)near(evaluate_exact_surface(g,.31,.57).point,
            {2.57*std::cos(.31),2.57*std::sin(.31),.57*.57});
    }
    auto cylinder=std::make_shared<ExactSurface>();cylinder->kind=ExactSurface::Kind::Cylinder;cylinder->radius=3;
    auto first=std::make_shared<ExactSurface>();first->kind=ExactSurface::Kind::Offset;first->basis=cylinder;first->offset=.3;
    auto second=std::make_shared<ExactSurface>();second->kind=ExactSurface::Kind::Offset;second->basis=first;second->offset=.4;
    g.support=second;g.validate();
    near(evaluate_exact_surface(g,.31,.57).point,{3.7*std::cos(.31),3.7*std::sin(.31),.57});
    derivatives(g,.31,.57,.2);
    g.support=cylinder;
    for(bool reversed:{false,true}) {
        g.reversed=reversed;
        const auto base=evaluate_exact_surface(g,.31,.57);
        const double offset=reversed?4.:-4.;
        const auto shifted=evaluate_exact_surface(g,.31,.57,offset);
        check(shifted.regular,"Regular offset beyond focal radius rejected");
        near(shifted.normal,base.normal);
        near(sub(shifted.point,scale(shifted.normal,offset)),base.point);
        derivatives(g,.31,.57,offset);
        check(!evaluate_exact_surface(g,0,.57,reversed?3.:-3.).regular,"Singular focal offset accepted");
    }
    g.reversed=false;
    auto singular=std::make_shared<ExactSurface>(*cylinder);singular->radius=0;g.support=singular;
    check(!evaluate_exact_surface(g,.31,.57).regular,"Singular support invented a normal");
}
void contacts() {
    auto g=bowl();const double u=.37,v=.61;
    const auto p=evaluate_exact_surface(g,u,v);
    const Vec3 seed{p.point.x+p.normal.x*.03,p.point.y+p.normal.y*.03,p.point.z+p.normal.z*.03};
    for(const auto& planes:std::vector<std::vector<std::pair<Vec3,double>>>{
            {},{{{1,0,0},u}},{{{1,0,0},u},{{0,1,0},v}}}) {
        const auto solved=solve_exact_surface_contact(g,seed,0,planes);
        check(solved.contacts.size()==1,"Bowl contact failed");near(solved.contacts.front().point,p.point,2e-7);
    }
    for(double offset:{-.01,0.,.02}) {
        const auto solved=solve_exact_surface_contact(g,seed,offset,{{{1,0,0},u},{{0,1,0},v}});
        check(solved.contacts.size()==1,"Offset contact failed");const auto& c=solved.contacts.front();
        near(c.point.x,u,1e-7);near(c.point.y,v,1e-7);
        const auto base=sub(c.point,scale(c.normal,offset));
        near(base.z,base.x*base.x+2*base.y*base.y,1e-7);
    }
    check(solve_exact_surface_contact(g,seed,0,{{{1,0,0},3}}).contacts.empty(),"Impossible bounded contact accepted");
    check(solve_exact_surface_contact(g,seed,0,{{{1,0,0},.3},{{1,0,0},.7}}).contacts.empty(),"Contradictory contact accepted");
    const auto line=[](Vec3 a,Vec3 b){return BSplineGeometry{1,{a,b},{0,0,1,1},{1,1}};};
    g.trim_loops={{line({0,0,0},{1,0,0}),line({1,0,0},{1,1,0}),line({1,1,0},{0,1,0}),line({0,1,0},{0,0,0})},
        {line({.3,.3,0},{.3,.7,0}),line({.3,.7,0},{.7,.7,0}),line({.7,.7,0},{.7,.3,0}),line({.7,.3,0},{.3,.3,0})}};
    check(exact_surface_domain_contains(g,.2,.4),"Outer trim rejected valid point");
    check(!exact_surface_domain_contains(g,.5,.5),"Trim hole accepted point");
    check(exact_surface_domain_contains(g,.3,.5),"Trim boundary rejected point");
    check(solve_exact_surface_contact(g,seed,0,{{{1,0,0},.5},{{0,1,0},.5}}).contacts.empty(),"Contact ignored trim hole");
    auto two=bowl();auto support=std::make_shared<ExactSurface>(*two.support);two.support=support;
    support->u_knots={-1,-1,-1,1,1,1};two.u_min=-1;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        support->poles[i*3+j]={double(i)-1,j*.5,(i==1?-1.:1.)+(j==2?2.:0)};
    const auto branches=solve_exact_surface_contact(two,{.1,0,1},0,{{{0,1,0},0},{{0,0,1},.25}},true);
    check(branches.discrete&&branches.contacts.size()==2,"Exact surface lost two solution branches");
    std::vector<double> roots;for(const auto& c:branches.contacts)roots.push_back(c.point.x);
    std::ranges::sort(roots);near(roots[0],-.5,2e-7);near(roots[1],.5,2e-7);
}
void continuous_contacts_and_concurrency() {
    auto g=bowl();auto support=std::make_shared<ExactSurface>(*g.support);g.support=support;
    for(unsigned u=0;u<3;++u)for(unsigned v=0;v<3;++v)support->poles[u*3+v].z=u==2?1.:0.;
    // z=x^2, with a free Y parameter. Intersection with its ruling is a line.
    for(double offset:{0.,-.02,.02}) {
        const auto seed=evaluate_exact_surface(g,.5,.37,offset).point;
        const auto continuous=solve_exact_surface_contact(g,seed,offset,
            {{{1,0,0},seed.x},{{0,0,1},seed.z}},true);
        check(!continuous.discrete&&!continuous.contacts.empty(),"Contained line became discrete alternatives");
        near(continuous.contacts.front().point,seed,2e-7);
    }
    // Tangency to z=x^2 in a fixed Y plane is an isolated double root.
    const auto tangent=solve_exact_surface_contact(g,{.1,.37,.01},0,
        {{{0,1,0},.37},{{0,0,1},0}},true);
    check(tangent.discrete&&!tangent.contacts.empty(),"Isolated tangent contact became continuous");
    check(tangent.contacts.size()==1,"Tangent contact produced duplicate solution branches");
    for(const auto& c:tangent.contacts)near(c.point,{0,.37,0},2e-7);
    const auto serial=evaluate_exact_surface(g,.23,.67,-.02);
    std::vector<std::future<void>> workers;
    for(unsigned i=0;i<4;++i)workers.push_back(std::async(std::launch::async,[&]{
        for(unsigned j=0;j<100;++j) {
            const auto p=evaluate_exact_surface(g,.23,.67,-.02);
            check(p.point==serial.point&&p.normal==serial.normal&&p.du==serial.du,"Concurrent evaluator changed output");
        }
    }));
    for(auto& worker:workers)worker.get();
    auto invalid=std::make_shared<ExactSurface>();invalid->kind=ExactSurface::Kind::Offset;
    invalid->offset=std::numeric_limits<double>::quiet_NaN();invalid->basis=support;
    auto outer=std::make_shared<ExactSurface>();outer->kind=ExactSurface::Kind::Offset;outer->basis=invalid;
    bool rejected=false;try{outer->validate();}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Nonfinite nested offset accepted");
}
void elliptical_offset_edits() {
    auto generator=std::make_shared<ExactSurfaceGenerator>();
    generator->kind=ExactSurfaceGenerator::Kind::Ellipse;
    generator->radius=40;generator->minor_radius=30;
    auto surface=std::make_shared<ExactSurface>();
    surface->kind=ExactSurface::Kind::Extrusion;surface->generator=generator;
    ExactSurfaceGeometry g;g.support=surface;g.u_max=2*std::acos(-1.);g.v_max=120;
    g.validate();
    for(double angle:{.16,.6,1.2,2.3,3.7,5.9})for(bool reverse:{false,true}) {
        g.reversed=reverse;
        const auto base=evaluate_exact_surface(g,angle,24.95);
        auto seed=base.point;
        for(double offset:{0.,1.,-1.,.5,-.5,0.}) {
            const auto solved=solve_exact_surface_contact(g,seed,offset);
            check(solved.contacts.size()==1,"Ellipse offset edit lost its contact");
            const Vec3 expected{base.point.x+offset*base.normal.x,
                base.point.y+offset*base.normal.y,base.point.z+offset*base.normal.z};
            near(solved.contacts.front().point,expected,2e-7);
            near(solved.contacts.front().normal,base.normal,1e-8);
            seed=solved.contacts.front().point;
        }
    }
}
}
int main() {
    try {
        polynomial_and_frames();rational_and_procedural();contacts();continuous_contacts_and_concurrency();elliptical_offset_edits();
        const auto g=bowl();const auto start=std::chrono::steady_clock::now();double sum=0;
        for(unsigned i=0;i<2000;++i)sum+=evaluate_exact_surface(g,(i%97)/97.,(i%83)/83.,.001).point.z;
        check(sum>0,"Evaluation checksum is empty");
        std::cout<<"Exact surfaces: polynomial/rational values, first/second derivatives, procedural/nested offsets, sides, reflections, scale and singular rejection passed; 2000 evaluations "
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
