#include <zima/cam/profile_milling.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>

using namespace zima;
namespace {
unsigned assertions{};
void check(bool yes,const char* text){++assertions;if(!yes)throw std::runtime_error(text);}
template<class F> void rejects(F f){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid milling accepted");}
kernel::ProjectionRequest rectangle(double w=20,double h=12) {
    kernel::ProjectionRequest request;request.sketch_id="profile";
    kernel::ExtrusionRequest p;p.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{0,0,0},{w,0,0},{w,h,0},{0,h,0}}};
    request.profiles.push_back(p);return request;
}
cam::ProfileMilling operation(){cam::ProfileMilling op;op.id="mill";op.name="Pocket";op.tool_id="tool";op.sketch_id="profile";
    op.top_mm=4;op.bottom_mm=0;op.step_down_mm=2;op.step_over_mm=1;return op;}
const cam::Tool tool{"tool","Flat mill",{{-10,1},{0,1}},{{0,1},{20,1}},300,6000,true};
const cam::Bounds stock{{-5,-5,-1},{30,30,4}},target{{-5,-5,-1},{30,30,0}};
cam::CompiledProgram generate(const cam::ProfileMilling& op,const kernel::ProjectionRequest& input) {
    static unsigned run{};std::cout<<"Case "<<++run<<" kind="<<int(op.kind)<<" side="<<int(op.side)<<std::endl;
    cam::Machine machine;machine.id="machine";machine.name="Mill";
    const auto program=cam::profile_program(op,tool,input,stock,target);
    check(program==cam::profile_program(op,tool,input,stock,target),"Path generation is not deterministic");
    return cam::compile(program,machine,{tool});
}
void samples(const cam::CompiledProgram& p,const std::function<void(cam::Point)>& test) {
    for(const auto& m:p.motions)if(m.kind!=cam::MotionKind::Rapid)for(unsigned i=0;i<=30;++i){const auto q=cam::point_at(m,i/30.);if(q.z<=4)test(q);}
}
}
int main()try {
    auto op=operation();auto input=rectangle();auto p=generate(op,input);
    samples(p,[](auto q){check(q.x>=1-1e-7&&q.x<=19+1e-7&&q.y>=1-1e-7&&q.y<=11+1e-7,"Rectangle offset crossed boundary");});
    bool level2{},level0{};for(const auto& m:p.motions){level2|=m.end.z==2;level0|=m.end.z==0;}
    check(level2&&level0,"Depth passes missing");
    op.kind=cam::ProfileMillingKind::Contour;
    for(bool cw:{false,true})for(auto side:{cam::ContourSide::Inside,cam::ContourSide::Outside}) {
        op.clockwise=cw;op.side=side;p=generate(op,input);
        samples(p,[&](auto q){const double d=std::min({std::abs(q.x),std::abs(q.x-20),std::abs(q.y),std::abs(q.y-12)});
            if(side==cam::ContourSide::Inside)check(std::abs(d-1)<1e-7,"Inside contour is not one radius from its wall");});
        for(const auto& m:p.motions)if(m.kind==cam::MotionKind::ArcClockwise||m.kind==cam::MotionKind::ArcCounterclockwise)
            check((m.kind==cam::MotionKind::ArcClockwise)==cw,"Wrong contour arc direction");
    }
    input.profiles.front().outer_profile=kernel::ExtrusionRequest::CircleProfile{{10,10,0},6};
    for(auto side:{cam::ContourSide::Inside,cam::ContourSide::Outside}) {
        op.side=side;op.radial_allowance_mm=.5;p=generate(op,input);
        samples(p,[&](auto q){check(std::abs(std::hypot(q.x-10,q.y-10)-(side==cam::ContourSide::Inside?4.5:7.5))<1e-7,"Circular compensation wrong");});
        check(std::ranges::any_of(p.motions,[](const auto& m){return m.kind==cam::MotionKind::ArcClockwise||m.kind==cam::MotionKind::ArcCounterclockwise;}),"Exact circle lost its arcs");
    }
    op=operation();input=rectangle();input.profiles.front().inner_profiles.push_back(kernel::ExtrusionRequest::CircleProfile{{10,6,0},2});
    p=generate(op,input);samples(p,[](auto q){check(std::hypot(q.x-10,q.y-6)>=3-1e-7,"Pocket crossed island");});
    // Concave L and two disconnected regions both retain their material domain.
    input=rectangle();input.profiles.front().outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{0,0,0},{12,0,0},{12,4,0},{4,4,0},{4,12,0},{0,12,0}}};
    p=generate(op,input);samples(p,[](auto q){check(q.x<=4+1e-7||q.y<=4+1e-7,"Concave pocket crossed retained corner");});
    input=rectangle(8,8);kernel::ExtrusionRequest::ProfileRegion extra;extra.outer_profile=kernel::ExtrusionRequest::CircleProfile{{20,4,0},4};input.profiles.front().additional_profile_regions.push_back(extra);
    p=generate(op,input);samples(p,[](auto q){check(q.x<=7+1e-7||std::hypot(q.x-20,q.y-4)<=3+1e-7,"Disconnected pocket traversed solid gap");});
    // Opposite Sketch normal does not silently reverse the authored CAM direction.
    const auto original=p;input.sketch_normal={0,0,-1};p=generate(op,input);check(p.motions.size()==original.motions.size(),"Reverse normal changed path extent");
    input.sketch_normal={1,0,0};rejects([&]{generate(op,input);});
    input=rectangle(1,1);rejects([&]{generate(op,input);});
    input=rectangle();op.bottom_mm=op.top_mm;rejects([&]{generate(op,input);});
    op=operation();op.step_over_mm=2;rejects([&]{generate(op,input);});
    op=operation();op.kind=cam::ProfileMillingKind::Contour;input.profiles.front().outer_profile=kernel::ExtrusionRequest::EllipseProfile{{10,10,0},{1,0,0},7,4};
    p=generate(op,input);check(!p.motions.empty(),"Bounded ellipse path missing");
    input.sketch_origin.z=7;std::get<kernel::ExtrusionRequest::EllipseProfile>(input.profiles.front().outer_profile).center.z=7;
    const auto elevated=generate(op,input);check(elevated.motions.size()==p.motions.size(),"Elevated ellipse changed path count");
    for(std::size_t i=0;i<p.motions.size();++i)check(p.motions[i].end==elevated.motions[i].end,"Sketch elevation replaced explicit machining depths");
    // Independent closest-point minimization on the exact ellipse bounds the
    // combined curve-flattening and cutter-compensation error.
    samples(p,[](auto q){const auto square=[&](double t){return std::pow(q.x-10-7*std::cos(t),2)+std::pow(q.y-10-4*std::sin(t),2);};
        double best=0;for(int i=1;i<128;++i){const double t=i*2*std::numbers::pi/128;if(square(t)<square(best))best=t;}
        double low=best-2*std::numbers::pi/128,high=best+2*std::numbers::pi/128;
        for(int i=0;i<50;++i){const double a=(2*low+high)/3,b=(low+2*high)/3;if(square(a)<square(b))high=b;else low=a;}
        const double d=std::sqrt(square((low+high)/2));check(d>=1-1e-7&&d<=1.01+1e-7,"Ellipse path exceeds declared tolerance or gouges its wall");});
    using R=kernel::ExtrusionRequest;
    input=rectangle();input.profiles.front().outer_profile=R::CurvedProfile{{R::LineCurve{{0,0,0},{8,0,0}},R::ArcCurve{{8,0,0},{12,4,0},{8,8,0}},R::LineCurve{{8,8,0},{0,8,0}},R::LineCurve{{0,8,0},{0,0,0}}}};
    op=operation();p=generate(op,input);samples(p,[](auto q){check(q.x>=1-1e-7&&q.y>=1-1e-7&&q.y<=7+1e-7&&(q.x<=8||std::hypot(q.x-8,q.y-4)<=3+1e-7),"Mixed analytic profile gouged its boundary");});
    // A narrow neck erodes into two separate accessible pockets.
    input=rectangle();input.profiles.front().outer_profile=R::PolygonProfile{{{0,0,0},{6,0,0},{6,2.5,0},{10,2.5,0},{10,0,0},{16,0,0},{16,6,0},{10,6,0},{10,3.5,0},{6,3.5,0},{6,6,0},{0,6,0}}};
    p=generate(op,input);bool left{},right{};samples(p,[&](auto q){left|=q.x<6;right|=q.x>10;check(q.x<6||q.x>10,"Tool crossed a narrower-than-diameter neck");});check(left&&right,"Split offset dropped a pocket");
    // A quadratic rational quarter circle is supplied as native spline data,
    // with its exact radius checked independently of the flattening routine.
    R::BSplineCurve spline;spline.start={4,0,0};spline.end={0,4,0};spline.degree=2;
    spline.control_points={{4,0,0},{4,4,0},{0,4,0}};spline.knots={0,0,0,1,1,1};spline.weights={1,std::sqrt(.5),1};
    input=rectangle();input.profiles.front().outer_profile=R::CurvedProfile{{spline,R::LineCurve{{0,4,0},{0,0,0}},R::LineCurve{{0,0,0},{4,0,0}}}};
    op.kind=cam::ProfileMillingKind::Contour;p=generate(op,input);
    samples(p,[](auto q){check(q.x>=1-1e-7&&q.y>=1-1e-7&&std::hypot(q.x,q.y)<=3+1e-7,"Native rational spline compensation gouged boundary");});
    for(double normal:{1.,-1.})for(bool reverse:{false,true}){
        const double orientation=normal*(reverse?-1:1);
        R::EllipticalArcCurve arc;arc.start={7,0,0};arc.end={-7,0,0};arc.center={};arc.major_axis_direction={1,0,0};arc.major_radius=7;arc.minor_radius=4;
        arc.start_parameter=0;arc.end_parameter=std::numbers::pi;arc.reversed=reverse;
        input=rectangle();input.sketch_normal.z=normal;input.profiles.front().outer_profile=R::CurvedProfile{{arc,R::LineCurve{{-7,0,0},{7,0,0}}}};
        p=generate(op,input);samples(p,[&](auto q){check(q.y*orientation>=1-1e-7,"Elliptical arc lost reversed normal/side");});
    }
    input=rectangle();unsigned checkpoints{};bool cancelled{};
    try{static_cast<void>(cam::profile_program(op,tool,input,stock,target,{},[&]{if(++checkpoints==5)throw std::runtime_error("cancelled");}));}catch(const std::runtime_error& e){cancelled=std::string(e.what())=="cancelled";}
    check(cancelled&&checkpoints==5,"Path cancellation was swallowed or delayed");
    input.profiles.front().outer_profile=R::PolygonProfile{{{0,0,0},{4,4,0},{0,4,0},{4,0,0}}};rejects([&]{generate(op,input);});
    op=operation();input=rectangle(8,2);p=generate(op,input);
    samples(p,[](auto q){check(std::abs(q.y-1)<1e-7&&q.x>=1-1e-7&&q.x<=7+1e-7,"Exactly fitting slot lost its centre line");});
    input.profiles.front().outer_profile=R::CircleProfile{{4,1,0},1};p=generate(op,input);
    samples(p,[](auto q){check(std::hypot(q.x-4,q.y-1)<1e-7,"Exactly fitting circular pocket lost its centre");});
    // Two distinct boundary edges legitimately share both endpoints. Preserve
    // a semicircle plus its diameter and a circle authored as two semicircles.
    input=rectangle();op.kind=cam::ProfileMillingKind::Contour;
    const R::ArcCurve upper{{4,0,0},{0,4,0},{-4,0,0}},lower{{-4,0,0},{0,-4,0},{4,0,0}};
    input.profiles.front().outer_profile=R::CurvedProfile{{upper,R::LineCurve{{-4,0,0},{4,0,0}}}};p=generate(op,input);
    samples(p,[](auto q){check(q.y>=1-1e-7&&std::hypot(q.x,q.y)<=3+1e-7,"Two-edge semicircle profile gouged its boundary");});
    input.profiles.front().outer_profile=R::CurvedProfile{{upper,lower}};p=generate(op,input);
    samples(p,[](auto q){check(std::abs(std::hypot(q.x,q.y)-3)<1e-7,"Two-arc circle changed its exact offset");});
    input.profiles.front().outer_profile=R::CurvedProfile{{upper,R::ArcCurve{{-4,0,0},{0,4,0},{4,0,0}}}};
    rejects([&]{generate(op,input);});
    std::cout<<"CAM profile milling: "<<assertions<<" analytic offsets, islands, concavity, depths, direction and rejection assertions passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
