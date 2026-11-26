#include <zima/cam/profile_milling.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>

namespace zima::cam {
namespace {
constexpr double tau=2*std::numbers::pi,eps=1e-8;
using V=Point;
using Request=kernel::ExtrusionRequest;
V add(V a,V b){return {a.x+b.x,a.y+b.y,0};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,0};}
V mul(V a,double s){return {a.x*s,a.y*s,0};}
double dot(V a,V b){return a.x*b.x+a.y*b.y;}
double cross(V a,V b){return a.x*b.y-a.y*b.x;}
double norm(V a){return std::hypot(a.x,a.y);}
bool near(V a,V b){return norm(sub(a,b))<eps;}
double angle(double a){a=std::fmod(a,tau);return a<0?a+tau:a;}
[[noreturn]] void invalid(){throw ProgramError(Error::InvalidOperation);}
struct Edge {V a,b,c;double sweep{};};
using Loop=std::vector<Edge>;
V value(const Edge& e,double t) {
    if(t==0)return e.a;if(t==1)return e.b;
    if(e.sweep==0)return add(e.a,mul(sub(e.b,e.a),t));
    const auto d=sub(e.a,e.c);const double a=e.sweep*t;
    return add(e.c,{d.x*std::cos(a)-d.y*std::sin(a),d.x*std::sin(a)+d.y*std::cos(a),0});
}
Edge reversed(Edge e){std::swap(e.a,e.b);e.sweep=-e.sweep;return e;}
double parameter(const Edge& e,V p) {
    if(e.sweep==0){const auto d=sub(e.b,e.a);return dot(sub(p,e.a),d)/dot(d,d);}
    const auto a=sub(e.a,e.c),b=sub(p,e.c);
    const double t=angle(std::atan2(cross(a,b),dot(a,b))*(e.sweep>0?1:-1))/std::abs(e.sweep);
    return near(p,e.a)?0:near(p,e.b)?1:t;
}
double distance(const Edge& e,V p) {
    if(e.sweep==0){const auto d=sub(e.b,e.a);return norm(sub(p,add(e.a,mul(d,std::clamp(parameter(e,p),0.,1.)))));}
    const auto d=sub(p,e.c);const double n=norm(d),r=norm(sub(e.a,e.c));
    if(n>eps&&parameter(e,add(e.c,mul(d,r/n)))<=1+1e-10)return std::abs(n-r);
    return std::min(norm(sub(p,e.a)),norm(sub(p,e.b)));
}
std::vector<V> intersections(const Edge& a,const Edge& b) {
    std::vector<V> hits;
    const auto offer=[&](V p){if(distance(a,p)<eps&&distance(b,p)<eps&&
        std::ranges::none_of(hits,[&](V q){return near(p,q);}))hits.push_back(p);};
    if(a.sweep==0&&b.sweep==0) {
        const auto u=sub(a.b,a.a),v=sub(b.b,b.a);const double det=cross(u,v);
        if(std::abs(det)>1e-14*norm(u)*norm(v))offer(add(a.a,mul(u,cross(sub(b.a,a.a),v)/det)));
        else for(V p:{a.a,a.b,b.a,b.b})offer(p);
    }else if(a.sweep==0||b.sweep==0) {
        const auto& line=a.sweep==0?a:b;const auto& arc=a.sweep==0?b:a;
        const auto d=sub(line.b,line.a),f=sub(line.a,arc.c);const double r=norm(sub(arc.a,arc.c));
        const double center=-dot(f,d)/dot(d,d),square=r*r-dot(f,f)+dot(f,d)*dot(f,d)/dot(d,d);
        if(square>=-eps*eps){const double delta=std::sqrt(std::max(0.,square)/dot(d,d));
            offer(add(line.a,mul(d,center-delta)));offer(add(line.a,mul(d,center+delta)));}
    }else {
        const double r=norm(sub(a.a,a.c)),s=norm(sub(b.a,b.c));const auto d=sub(b.c,a.c);const double n=norm(d);
        if(n<eps){if(std::abs(r-s)<eps)for(V p:{a.a,a.b,b.a,b.b})offer(p);}
        else if(n<=r+s+eps&&n>=std::abs(r-s)-eps) {
            const double x=(n*n+r*r-s*s)/(2*n),h=std::sqrt(std::max(0.,r*r-x*x));
            const auto base=add(a.c,mul(d,x/n)),off=mul(V{-d.y,d.x,0},h/n);
            offer(add(base,off));offer(sub(base,off));
        }
    }
    return hits;
}
void circle(Loop& out,V c,double r) {
    const std::array<V,4> vertices{{{c.x+r,c.y,0},{c.x,c.y+r,0},{c.x-r,c.y,0},{c.x,c.y-r,0}}};
    for(unsigned i=0;i<4;++i)out.push_back({vertices[i],vertices[(i+1)%4],c,tau/4});
}
struct Region {
    std::vector<Loop> loops;
    double approximation{};
    bool contains(V p) const {
        // Split every arc at its vertical extrema. The half-open crossing rule
        // counts a shared vertex once and does not treat a tangency as crossing.
        bool inside{};
        for(const auto& loop:loops)for(const auto& e:loop) {
            std::vector<double> cuts{0,1};
            if(e.sweep!=0){const double r=norm(sub(e.a,e.c));for(double sign:{-1.,1.}){
                const double t=parameter(e,{e.c.x,e.c.y+sign*r,0});if(t>0&&t<1)cuts.push_back(t);}}
            std::ranges::sort(cuts);
            for(std::size_t i=1;i<cuts.size();++i) {
                const auto a=value(e,cuts[i-1]),b=value(e,cuts[i]);
                if((a.y>p.y)==(b.y>p.y))continue;
                double x;
                if(e.sweep==0)x=a.x+(p.y-a.y)*(b.x-a.x)/(b.y-a.y);
                else{const double r=norm(sub(e.a,e.c)),dy=p.y-e.c.y;
                    const double sign=value(e,(cuts[i-1]+cuts[i])/2).x>=e.c.x?1:-1;
                    x=e.c.x+sign*std::sqrt(std::max(0.,r*r-dy*dy));}
                if(x>p.x)inside=!inside;
            }
        }
        return inside;
    }
    double separation(V p) const {double d=std::numeric_limits<double>::infinity();
        for(const auto& loop:loops)for(const auto& e:loop)d=std::min(d,distance(e,p));return d;}
};
void check_loop(const Loop& loop) {
    if(loop.empty())invalid();
    for(std::size_t i=0;i<loop.size();++i) {
        const auto& e=loop[i];if(!near(e.b,loop[(i+1)%loop.size()].a)||near(e.a,e.b))invalid();
        for(double v:{e.a.x,e.a.y,e.b.x,e.b.y,e.c.x,e.c.y,e.sweep})if(!std::isfinite(v))invalid();
        if(e.sweep!=0&&(norm(sub(e.a,e.c))<eps||std::abs(norm(sub(e.a,e.c))-norm(sub(e.b,e.c)))>eps))invalid();
    }
}
Region region(const kernel::ProjectionRequest& input,double tolerance,const std::function<void()>& checkpoint) {
    for(double v:{input.sketch_origin.x,input.sketch_origin.y,input.sketch_origin.z,input.sketch_normal.x,input.sketch_normal.y,input.sketch_normal.z})if(!std::isfinite(v))invalid();
    if(!std::isfinite(input.sketch_normal.z)||std::abs(std::abs(input.sketch_normal.z)-1)>1e-9||
        std::hypot(input.sketch_normal.x,input.sketch_normal.y)>1e-9)invalid();
    Region result;
    const auto planar=[&](V p){if(!std::isfinite(p.z)||std::abs(p.z-input.sketch_origin.z)>eps)invalid();p.z=0;return p;};
    const auto append=[&](const Request::ProfileLoop& profile) {
        Loop out;
        const auto line=[&](V a,V b){a=planar(a);b=planar(b);if(!near(a,b))out.push_back({a,b,{},{}});};
        const auto spline=[&](kernel::BSplineGeometry curve) {
            curve=sketcher::clamp_curve_geometry(std::move(curve));
            for(auto& p:curve.poles)p=planar(p);
            const auto flatten=[&](auto&& self,const kernel::BSplineGeometry& span,unsigned depth)->void {
                if(checkpoint)checkpoint();
                const Edge chord{span.poles.front(),span.poles.back(),{},0};
                double bound{};for(const auto& p:span.poles)bound=std::max(bound,near(chord.a,chord.b)?norm(sub(p,chord.a)):distance(chord,p));
                if(bound<=tolerance/2){if(!near(chord.a,chord.b))out.push_back(chord);return;}
                if(depth>=30||out.size()>4096)invalid();
                self(self,sketcher::trim_curve_geometry(span,0,.5),depth+1);
                self(self,sketcher::trim_curve_geometry(span,.5,1),depth+1);
            };
            flatten(flatten,curve,0);result.approximation=tolerance/2;
        };
        const auto ellipse=[&](V c,V major,double rx,double ry,double first,double last,bool reverse) {
            c=planar(c);if(std::abs(major.z)>eps||rx<=0||ry<=0||!(last>first))invalid();
            const double n=norm(major);if(n<eps)invalid();major=mul(major,1/n);
            const double orientation=(reverse?-1.:1.)*input.sketch_normal.z;
            const V minor{-orientation*major.y,orientation*major.x,0};
            auto curve=sketcher::circular_curve_geometry({},1,first,last);
            for(auto& p:curve.poles)p=add(c,add(mul(major,p.x*rx),mul(minor,p.y*ry)));
            // The transient spline lies in XY after the explicit frame check.
            for(auto& p:curve.poles)p.z=input.sketch_origin.z;
            spline(std::move(curve));
        };
        std::visit([&](const auto& p){using T=std::decay_t<decltype(p)>;
            if constexpr(std::is_same_v<T,Request::PolygonProfile>) {
                if(p.vertices.size()<3)invalid();for(std::size_t i=0;i<p.vertices.size();++i)line(p.vertices[i],p.vertices[(i+1)%p.vertices.size()]);
            }else if constexpr(std::is_same_v<T,Request::CircleProfile>) {
                if(!std::isfinite(p.radius)||p.radius<=eps)invalid();circle(out,planar(p.center),p.radius);
            }else if constexpr(std::is_same_v<T,Request::EllipseProfile>)ellipse(p.center,p.major_axis_direction,p.major_radius,p.minor_radius,0,tau,false);
            else for(const auto& item:p.curves)std::visit([&](const auto& c){using C=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<C,Request::LineCurve>)line(c.start,c.end);
                else if constexpr(std::is_same_v<C,Request::ArcCurve>) {
                    const auto a=planar(c.start),b=planar(c.middle),end=planar(c.end),u=sub(b,a),v=sub(end,a);
                    const double d=2*cross(u,v);if(std::abs(d)<1e-14*norm(u)*norm(v))invalid();
                    const V center=add(a,{(dot(u,u)*v.y-dot(v,v)*u.y)/d,(u.x*dot(v,v)-v.x*dot(u,u))/d,0});
                    const auto x=sub(a,center),y=sub(end,center);double sweep=angle(std::atan2(cross(x,y),dot(x,y)));
                    const auto mid=sub(b,center);if(angle(std::atan2(cross(x,mid),dot(x,mid)))>sweep)sweep-=tau;
                    out.push_back({a,end,center,sweep});
                }else if constexpr(std::is_same_v<C,Request::EllipticalArcCurve>)ellipse(c.center,c.major_axis_direction,c.major_radius,c.minor_radius,c.start_parameter,c.end_parameter,c.reversed);
                else {if(c.interpolating||c.knots.empty())invalid();spline({c.degree,c.control_points,c.knots,c.weights});}
            },item);
        },profile);
        check_loop(out);result.loops.push_back(std::move(out));
    };
    for(const auto& request:input.profiles) {
        append(request.outer_profile);for(const auto& p:request.inner_profiles)append(p);
        for(const auto& r:request.additional_profile_regions){append(r.outer_profile);for(const auto& p:r.inner_profiles)append(p);}
    }
    if(result.loops.empty())invalid();
    // Reject crossing/overlapping authored boundaries before offsetting. Each
    // closed component owns one unambiguous even-odd material region.
    for(std::size_t i=0;i<result.loops.size();++i)for(std::size_t j=i;j<result.loops.size();++j)
    for(std::size_t a=0;a<result.loops[i].size();++a)for(std::size_t b=j==i?a+1:0;b<result.loops[j].size();++b) {
        if(checkpoint&&b%32==0)checkpoint();
        const auto& x=result.loops[i][a];const auto& y=result.loops[j][b];const auto hits=intersections(x,y);
        const bool adjacent=i==j&&(b==a+1||(a==0&&b+1==result.loops[i].size()));
        for(V p:hits)if(!adjacent||!((near(p,x.a)||near(p,x.b))&&(near(p,y.a)||near(p,y.b))))invalid();
        // A two-edge closed profile shares both endpoints (for example an
        // arc and its chord). Coincident/retraced edges still enclose no region.
        if(hits.size()>1&&(!adjacent||result.loops[i].size()!=2||
            distance(x,value(y,.5))<eps||distance(y,value(x,.5))<eps))invalid();
    }
    return result;
}
struct Box {double x0,y0,x1,y1;};
Box bounds(const Edge& e){Box b{std::min(e.a.x,e.b.x),std::min(e.a.y,e.b.y),std::max(e.a.x,e.b.x),std::max(e.a.y,e.b.y)};
    if(e.sweep!=0){const double r=norm(sub(e.a,e.c));for(V p:{V{e.c.x-r,e.c.y,0},V{e.c.x+r,e.c.y,0},V{e.c.x,e.c.y-r,0},V{e.c.x,e.c.y+r,0}})
        if(parameter(e,p)<=1){b.x0=std::min(b.x0,p.x);b.x1=std::max(b.x1,p.x);b.y0=std::min(b.y0,p.y);b.y1=std::max(b.y1,p.y);}}
    return b;
}
std::vector<Loop> offset(const Region& source,double amount,bool inside,bool collapsed,const std::function<void()>& checkpoint) {
    Loop candidates;
    for(const auto& loop:source.loops)for(const auto& e:loop) {
        if(e.sweep==0){const auto d=sub(e.b,e.a);const V n=mul(V{-d.y,d.x,0},amount/norm(d));
            for(double sign:{-1.,1.})candidates.push_back({add(e.a,mul(n,sign)),add(e.b,mul(n,sign)),{},0});}
        else {const double r=norm(sub(e.a,e.c));for(double rr:{r-amount,r+amount})if(rr>eps)
            candidates.push_back({add(e.c,mul(sub(e.a,e.c),rr/r)),add(e.c,mul(sub(e.b,e.c),rr/r)),e.c,e.sweep});}
        circle(candidates,e.a,amount);
    }
    if(candidates.size()>32768)invalid();
    std::vector<std::vector<double>> cuts(candidates.size(),{0,1});
    std::vector<Box> boxes;std::vector<std::size_t> order;
    for(std::size_t i=0;i<candidates.size();++i){boxes.push_back(bounds(candidates[i]));order.push_back(i);}
    std::ranges::stable_sort(order,[&](auto i,auto j){return boxes[i].x0<boxes[j].x0;});
    // Exact arc extrema bound the broad phase; only provably disjoint pairs
    // are skipped. Preserve candidate order when assembling the final paths.
    for(std::size_t a=0;a<order.size();++a){if(checkpoint)checkpoint();const auto i=order[a];
        for(std::size_t b=a+1;b<order.size();++b){const auto j=order[b];
            if(boxes[j].x0>boxes[i].x1+eps)break;
            if(boxes[j].y0>boxes[i].y1+eps||boxes[i].y0>boxes[j].y1+eps)continue;
            for(V p:intersections(candidates[i],candidates[j])){cuts[i].push_back(std::clamp(parameter(candidates[i],p),0.,1.));cuts[j].push_back(std::clamp(parameter(candidates[j],p),0.,1.));}
        }
    }
    Loop pieces,medial;
    for(std::size_t i=0;i<candidates.size();++i) {
        if(checkpoint)checkpoint();
        auto& ts=cuts[i];std::ranges::sort(ts);ts.erase(std::unique(ts.begin(),ts.end(),[](double a,double b){return std::abs(a-b)<1e-12;}),ts.end());
        const auto& e=candidates[i];
        for(std::size_t j=1;j<ts.size();++j) {
            const auto p=value(e,(ts[j-1]+ts[j])/2);
            if(source.contains(p)!=inside||std::abs(source.separation(p)-amount)>eps)continue;
            Edge part{value(e,ts[j-1]),value(e,ts[j]),e.c,e.sweep*(ts[j]-ts[j-1])};
            if(near(part.a,part.b))continue;
            auto tangent=e.sweep==0?sub(e.b,e.a):V{-(p.y-e.c.y),p.x-e.c.x,0};
            const auto normal=mul(V{-tangent.y,tangent.x,0},std::max(32*eps,std::min(1e-5,amount*1e-5))/norm(tangent));
            const auto feasible=[&](V q){return source.contains(q)==inside&&source.separation(q)>amount;};
            const bool left=feasible(add(p,normal)),right=feasible(sub(p,normal));
            // A cutter that exactly fits a slot has a line of valid centres.
            // Keep that locus on the first pocket pass; later collapsed passes
            // are already covered by the preceding pass and need no recutting.
            const bool is_medial=!left&&!right;
            if(left==right&&(!collapsed||!is_medial))continue;
            auto& selected=is_medial?medial:pieces;
            if(std::ranges::any_of(selected,[&](const auto& q){return near(value(part,.5),value(q,.5))&&
                ((near(part.a,q.a)&&near(part.b,q.b))||(near(part.a,q.b)&&near(part.b,q.a)));}))continue;
            selected.push_back(part);
        }
    }
    std::vector<Loop> loops;
    if(std::getenv("ZIMA_CPP_PROFILE_CAM"))std::fprintf(stderr,"CAM offset: distance=%.17g candidates=%zu pieces=%zu\n",amount,candidates.size(),pieces.size());
    while(!pieces.empty()) {
        if(checkpoint)checkpoint();
        Loop loop{pieces.back()};pieces.pop_back();
        while(!near(loop.back().b,loop.front().a)) {
            auto found=std::ranges::find_if(pieces,[&](const auto& e){return near(e.a,loop.back().b)||near(e.b,loop.back().b);});
            if(found==pieces.end()){
                if(std::getenv("ZIMA_CPP_PROFILE_CAM"))std::fprintf(stderr,"CAM open offset: start=(%.17g,%.17g) end=(%.17g,%.17g) edges=%zu\n",loop.front().a.x,loop.front().a.y,loop.back().b.x,loop.back().b.y,loop.size());
                invalid();
            }
            auto e=*found;if(!near(e.a,loop.back().b))e=reversed(e);loop.push_back(e);pieces.erase(found);
        }
        check_loop(loop);loops.push_back(std::move(loop));
    }
    while(!medial.empty()){
        Loop path{medial.back()};medial.pop_back();
        for(bool front:{false,true})for(;;){if(checkpoint)checkpoint();const auto end=front?path.front().a:path.back().b;
            const auto it=std::ranges::find_if(medial,[&](const auto& e){return near(e.a,end)||near(e.b,end);});if(it==medial.end())break;
            auto e=*it;if(front?!near(e.b,end):!near(e.a,end))e=reversed(e);medial.erase(it);
            if(front)path.insert(path.begin(),e);else path.push_back(e);
        }
        loops.push_back(std::move(path));
    }
    if(collapsed)for(const auto& loop:source.loops)for(const auto& e:loop)if(e.sweep!=0&&std::abs(norm(sub(e.a,e.c))-amount)<eps&&
            source.contains(e.c)==inside&&std::abs(source.separation(e.c)-amount)<eps){
        const bool covered=std::ranges::any_of(loops,[&](const auto& path){return std::ranges::any_of(path,[&](const auto& edge){return near(edge.a,edge.b)?near(e.c,edge.a):distance(edge,e.c)<eps;});});
        if(!covered)loops.push_back({{e.c,e.c,{},0}});
    }
    return loops;
}
double area(const Loop& loop) {
    double twice{};for(const auto& e:loop)twice+=cross(e.a,e.b)+(e.sweep==0?0:dot(sub(e.a,e.c),sub(e.a,e.c))*(e.sweep-std::sin(e.sweep)));
    return twice/2;
}
}
void validate(const ProfileMilling& p) {
    if(p.id.empty()||p.name.empty()||p.tool_id.empty()||p.sketch_id.empty()||
        (p.kind!=ProfileMillingKind::Pocket&&p.kind!=ProfileMillingKind::Contour)||
        (p.side!=ContourSide::Inside&&p.side!=ContourSide::Outside))invalid();
    for(double v:{p.top_mm,p.bottom_mm,p.step_down_mm,p.step_over_mm,p.clearance_mm,p.radial_allowance_mm,p.axial_allowance_mm,p.path_tolerance_mm})if(!std::isfinite(v))invalid();
    if(p.bottom_mm+p.axial_allowance_mm>=p.top_mm||p.step_down_mm<=0||p.step_over_mm<=0||p.clearance_mm<=0||
        p.radial_allowance_mm<0||p.axial_allowance_mm<0||p.path_tolerance_mm<=0)invalid();
}
Program profile_program(const ProfileMilling& op,const Tool& tool,const kernel::ProjectionRequest& input,
        const Bounds& stock,const Bounds& target,const std::vector<Bounds>& fixtures,const std::function<void()>& checkpoint) {
    validate(op);validate(tool);if(op.tool_id!=tool.id||op.sketch_id!=input.sketch_id)invalid();
    const double radius=tool.cutting.front().radius_mm;
    // This 2.5D strategy uses a cylindrical, flat cutting portion. Other tool
    // profiles remain available to manual/facing programs and exact simulation.
    if(radius<=eps||std::ranges::any_of(tool.cutting,[&](const auto& p){return p.radius_mm!=radius;})||
        op.step_down_mm>-tool.cutting.front().z_mm||(op.kind==ProfileMillingKind::Pocket&&op.step_over_mm>radius))invalid();
    const auto source=region(input,op.path_tolerance_mm,checkpoint);
    std::vector<Loop> paths;
    const double first=radius+op.radial_allowance_mm+source.approximation;
    const bool inside=op.kind==ProfileMillingKind::Pocket||op.side==ContourSide::Inside;
    for(std::size_t pass=0;;++pass) {
        if(checkpoint)checkpoint();if(pass>100000)invalid();auto loops=offset(source,first+pass*op.step_over_mm,inside,op.kind==ProfileMillingKind::Pocket&&pass==0,checkpoint);
        if(loops.empty())break;
        paths.insert(paths.end(),std::make_move_iterator(loops.begin()),std::make_move_iterator(loops.end()));
        if(paths.size()>100000)invalid();
        if(op.kind==ProfileMillingKind::Contour)break;
    }
    if(paths.empty())invalid();
    const double finish=op.bottom_mm+op.axial_allowance_mm;
    const double layers=std::ceil((op.top_mm-finish)/op.step_down_mm);if(!std::isfinite(layers)||layers>1000000)invalid();
    double clear=std::max({op.top_mm,stock.maximum.z,target.maximum.z});for(const auto& b:fixtures)clear=std::max(clear,b.maximum.z);clear+=op.clearance_mm;
    if(!std::isfinite(clear))invalid();
    for(auto& loop:paths)if(near(loop.front().a,loop.back().b)&&(area(loop)<0)!=op.clockwise){std::ranges::reverse(loop);for(auto& e:loop)e=reversed(e);}
    Program program;auto p=paths.front().front().a;p.z=clear;program.initial_tip=p;
    const auto command=[&](BlockKind kind,double v){Block b;b.id=op.id+":command:"+std::to_string(program.blocks.size());b.operation_id=op.id;b.kind=kind;b.tool_id=tool.id;b.value=v;b.clockwise=tool.clockwise;program.blocks.push_back(std::move(b));};
    command(BlockKind::Tool,0);command(BlockKind::Spindle,tool.spindle_rpm);command(BlockKind::Feed,tool.feed_mm_min);
    const auto move=[&](MotionKind kind,V end,V center=V{}) {
        if(program.blocks.size()>=1000000)invalid();
        if(near(p,end)&&std::abs(p.z-end.z)<eps&&kind!=MotionKind::ArcClockwise&&kind!=MotionKind::ArcCounterclockwise)return;
        Block b;b.id=op.id+":motion:"+std::to_string(program.blocks.size());b.operation_id=op.id;b.kind=BlockKind::Motion;b.motion=kind;b.end=end;b.center=center;
        program.blocks.push_back(std::move(b));p=end;
    };
    for(std::size_t layer=0;layer<std::size_t(layers);++layer) {
        if(checkpoint)checkpoint();
        const double z=std::max(finish,op.top_mm-(layer+1)*op.step_down_mm);
        for(const auto& loop:paths) {
            auto start=loop.front().a;start.z=clear;move(MotionKind::Rapid,start);
            start.z=z;move(MotionKind::Linear,start); // Cutting plunge, never a rapid through stock.
            for(const auto& e:loop){auto end=e.b,center=e.c;end.z=center.z=z;
                move(e.sweep==0?MotionKind::Linear:e.sweep<0?MotionKind::ArcClockwise:MotionKind::ArcCounterclockwise,end,center);}
            auto retract=p;retract.z=clear;move(MotionKind::Rapid,retract);
        }
    }
    return program;
}
} // namespace zima::cam
