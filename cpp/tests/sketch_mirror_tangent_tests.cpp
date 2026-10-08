#include <zima/sketcher/sketch.hpp>
#include "../app/sketch_tangent_test_equations.hpp"
#include <zima/sketcher/sketch_trim.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace zima::sketcher;
namespace {
constexpr double pi=3.14159265358979323846;
using P=std::array<double,2>;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double distance(P a,P b){return std::hypot(a[0]-b[0],a[1]-b[1]);}
P point(const Sketch& s,const std::string& id){auto p=s.find_point(id);check(p,"Missing point");return {p->x,p->y};}
void solved(Sketch& s){auto r=s.solve();if(r.status==SolveStatus::Conflicting||r.status==SolveStatus::Invalid){throw std::runtime_error("Tangent system cannot solve: status "+std::to_string(static_cast<int>(r.status))+" residual "+std::to_string(r.maximum_residual));}}
void tangent(const Sketch& s,const std::string& line,const std::string& circle,bool contact) {
    const auto& l=*std::ranges::find(s.segments,line,&SketchSegment::id);
    const auto& c=*std::ranges::find(s.circles,circle,&SketchCircle::id);
    auto a=point(s,l.first_point_id),b=point(s,l.second_point_id),o=point(s,c.center_point_id);
    double length=distance(a,b),dx=b[0]-a[0],dy=b[1]-a[1];check(length>1e-9,"Zero tangent segment");
    check(std::abs(std::abs((o[0]-a[0])*dy-(o[1]-a[1])*dx)/length-c.radius)<1e-5,"Independent line/circle tangency equation failed");
    double t=((o[0]-a[0])*dx+(o[1]-a[1])*dy)/(length*length);
    check(t>=-1e-6&&t<=1+1e-6,"Tangent contact escaped finite segment");
    if(contact)check(std::min(std::abs(distance(a,o)-c.radius),std::abs(distance(b,o)-c.radius))<1e-5,"Common tangent end left circle");
    check(std::ranges::any_of(s.constraints,[&](const auto& r){return r.kind==ConstraintKind::Tangent&&!r.suppressed&&((r.geometry_id==line&&r.second_geometry_id==circle)||(r.geometry_id==circle&&r.second_geometry_id==line));}),"Persistent second T missing");
}
std::vector<std::pair<P,P>> candidates(P a,double ra,P b,double rb) {
    const double d=distance(a,b);std::vector<std::pair<P,P>> out;if(d<1e-12)return out;
    const double angle=std::atan2(b[1]-a[1],b[0]-a[0]);
    for(double side:{-1.,1.}) {
        const double cosine=(ra-side*rb)/d;if(std::abs(cosine)>1)continue;
        for(double branch:{-1.,1.}) {double theta=angle+branch*std::acos(cosine);P n{std::cos(theta),std::sin(theta)};
            P p{a[0]+ra*n[0],a[1]+ra*n[1]},q{b[0]+side*rb*n[0],b[1]+side*rb*n[1]};if(distance(p,q)>1e-9)out.emplace_back(p,q);
        }
    }return out;
}
Sketch user_sketch(){std::ifstream file("cpp/tests/fixtures/sketch/form-edge-tangents.json");check(bool(file),"FORM-EDGE fixture missing");std::stringstream buffer;buffer<<file.rdbuf();return Sketch::from_serialized(buffer.str());}
void user_cases() {
    for(bool reverse:{false,true}) {
        auto s=user_sketch();auto small=s.circles[0].id,left=s.circles[2].id,line=s.segments.back().id;
        static_cast<void>(s.add_tangent_constraint(reverse?small:line,reverse?line:small));solved(s);tangent(s,line,small,false);tangent(s,line,left,true);
        check(s.constraints.size()==19,"Second T did not retain its C contact");
        s=Sketch::from_serialized(s.serialized());solved(s);tangent(s,line,small,false);
        auto common=user_sketch();auto id=common.add_common_tangent_segment(reverse?left:small,reverse?P{-4.85,-1.97}:P{-1.8,-9.7},reverse?small:left,reverse?P{-1.8,-9.7}:P{-4.85,-1.97});
        tangent(common,id,small,true);tangent(common,id,left,true);
        for(double radius:{2.1,1.8,2.05}) {auto d=common.create_circle_radius_dimension(small);if(common.dimensions.empty())common.apply_dimension(d);check(common.set_dimension_value(common.dimensions.front().id,radius),"FORM-EDGE radius edit rejected");solved(common);tangent(common,id,small,true);tangent(common,id,left,true);}
    }
    std::cout<<"FORM-EDGE: both T selection orders, common tangent, successive radius edits and reopen passed\n";
}
void quadrant_matrix() {
    std::size_t count{};
    for(P origin:{P{-17,-13},P{17,13},P{0,0}})
    for(int orientation=0;orientation<8;++orientation)
    for(auto radii:{P{2,2},P{.1,7},P{7,.1},P{2,3},P{.001,.002},P{20,30}})
    for(bool reverse:{false,true})for(int qa=0;qa<4;++qa)for(int qb=0;qb<4;++qb) {
        auto s=Sketch::create_default();double angle=orientation*pi/4;
        P a=origin,b{a[0]+(radii[0]+radii[1]+8)*std::cos(angle),a[1]+(radii[0]+radii[1]+8)*std::sin(angle)};
        auto first=s.add_circle(a[0],a[1],radii[0]),second=s.add_circle(b[0],b[1],radii[1]);
        auto circles=s.circles;P ha{a[0]+radii[0]*std::cos((qa+.5)*pi/2),a[1]+radii[0]*std::sin((qa+.5)*pi/2)},hb{b[0]+radii[1]*std::cos((qb+.5)*pi/2),b[1]+radii[1]*std::sin((qb+.5)*pi/2)};
        auto id=s.add_common_tangent_segment(reverse?second:first,reverse?hb:ha,reverse?first:second,reverse?ha:hb);
        tangent(s,id,first,true);tangent(s,id,second,true);
        const auto& l=s.segments.back();auto pa=point(s,reverse?l.second_point_id:l.first_point_id),pb=point(s,reverse?l.first_point_id:l.second_point_id);
        double best=1e100;for(auto [p,q]:candidates(a,radii[0],b,radii[1]))best=std::min(best,distance(p,ha)+distance(q,hb));
        check(distance(pa,ha)+distance(pb,hb)<=best+1e-5,"Click quadrants selected a non-nearest tangent branch");
        for(std::size_t i=0;i<2;++i){check(distance(point(s,circles[i].center_point_id),i==0?a:b)<1e-6,"Creating tangent moved source center");check(std::abs(s.circles[i].radius-radii[i])<1e-6,"Creating tangent changed source radius");}
        if(count%97==0){s=Sketch::from_serialized(s.serialized());solved(s);tangent(s,id,first,true);tangent(s,id,second,true);}++count;
    }
    std::cout<<count<<" common tangent quadrant/size/orientation/order combinations passed\n";
}
void invalid_cases(){for(auto data:{std::array{0.,2.,2.},std::array{1.,4.,1.},std::array{2.9,4.,1.}}){auto s=Sketch::create_default();auto a=s.add_circle(0,0,data[1]),b=s.add_circle(data[0],0,data[2]);auto before=s.serialized();bool rejected=false;try{static_cast<void>(s.add_common_tangent_segment(a,{0,data[1]},b,{data[0],data[2]}));}catch(const std::exception&){rejected=true;}check(rejected&&s.serialized()==before,"Impossible tangent partly changed Sketch");}}
void edit_matrix() {
    std::size_t edits{};
    for(bool diameter:{false,true})for(bool locked:{false,true})for(bool driving:{false,true})
    for(bool external:{false,true})for(bool reverse:{false,true})for(int branch=0;branch<4;++branch) {
        auto s=Sketch::create_default();auto a=s.add_circle(-12,-8,2),b=s.add_circle(8,5,3);
        auto center=s.circles.front().center_point_id;
        if(external){auto r=Sketch::create_external_reference(ExternalReferenceKind::Point);r.source_document_id="fixture";r.source_owner_id="source";r.source_semantic_key="point:anchor";r.cached_points={{-12,-8}};s.add_external_reference(r);static_cast<void>(s.add_point_reference_constraint(center,r.id));}
        auto choices=candidates({-12,-8},2,{8,5},3);auto [p,q]=choices[branch];
        auto line=s.add_common_tangent_segment(reverse?b:a,reverse?q:p,reverse?a:b,reverse?p:q);
        auto d=diameter?s.create_circle_diameter_dimension(a):s.create_circle_radius_dimension(a);d.locked=locked;d.driving=driving;
        if(!driving&&locked){auto before=s.serialized();bool rejected=false;try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&s.serialized()==before,"Locked reference dimension partly changed Sketch");continue;}
        s.apply_dimension(d);
        auto refs=s.external_references;
        for(double radius:{2.15,1.85,2.05}) {
            if(driving)check(s.set_dimension_value(d.id,diameter?2*radius:radius),"Common tangent R/D edit rejected");
            auto old=point(s,s.circles.back().center_point_id);check(s.move_point(s.circles.back().center_point_id,old[0]+.4,old[1]+.3),"Free circle center drag rejected");
            check(distance(old,point(s,s.circles.back().center_point_id))>.1,"Circle center did not move with a free DOF");
            solved(s);tangent(s,line,a,true);tangent(s,line,b,true);
            check(s.external_references==refs,"Drag modified read-only external anchor");
            const auto& actual=s.dimensions.front();check(std::abs(actual.value-(diameter?2:1)*s.circles.front().radius)<1e-5,"R/D reference or driver lost actual radius");
            s=Sketch::from_serialized(s.serialized());solved(s);tangent(s,line,a,true);tangent(s,line,b,true);++edits;
        }
    }
    std::cout<<edits<<" successive R/D edits, center drags, lock/reference states, external anchors and reopen passed\n";
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle}) {
        auto s=Sketch::create_default();auto a=s.add_circle(-12,-8,2),b=s.add_circle(8,5,3);auto [p,q]=candidates({-12,-8},2,{8,5},3).front();auto line=s.add_common_tangent_segment(a,p,b,q);
        auto d=s.create_segment_dimension(line,kind);s.apply_dimension(d);
        for(double factor:{1.02,.99,1.01}){check(s.set_dimension_value(d.id,dimension_display_value(d)*factor),"Common tangent line dimension edit rejected");solved(s);tangent(s,line,a,true);tangent(s,line,b,true);}
    }
}
void support_matrix() {
    for(bool reverse:{false,true})for(int branch=0;branch<4;++branch)for(bool both_external:{false,true}) {
        auto s=Sketch::create_default();
        const auto external_circle=[&](P center,double radius){auto r=Sketch::create_external_reference(ExternalReferenceKind::Edge);r.source_document_id="fixture";r.source_owner_id="circle-source";r.source_semantic_key="circle:"+std::to_string(s.external_references.size());for(int i=0;i<=32;++i){double t=i*2*pi/32;r.cached_points.push_back({center[0]+radius*std::cos(t),center[1]+radius*std::sin(t)});}s.add_external_reference(r);return r.id;};
        auto a=both_external?external_circle({-12,-8},2):s.add_circle(-12,-8,2),b=external_circle({8,5},3);auto [p,q]=candidates({-12,-8},2,{8,5},3)[branch];
        auto line=s.add_common_tangent_segment(reverse?b:a,reverse?q:p,reverse?a:b,reverse?p:q);auto refs=s.external_references;
        for(int edit=0;edit<3;++edit){for(auto& v:s.external_references.back().cached_points){v[0]+=.4;v[1]+=.3;}auto refreshed=s.external_references;solved(s);check(s.external_references==refreshed,"Solve changed read-only circle support");const auto& l=s.segments.front();auto first=point(s,l.first_point_id),last=point(s,l.second_point_id);double length=distance(first,last);for(auto [center,radius]:{std::pair{P{-12,-8},2.},std::pair{P{8+.4*(edit+1),5+.3*(edit+1)},3.}})check(std::abs(std::abs((center[0]-first[0])*(last[1]-first[1])-(center[1]-first[1])*(last[0]-first[0]))/length-radius)<1e-5,"Refreshed circle double tangent equation failed");s=Sketch::from_serialized(s.serialized());}
    }
    for(int branch=0;branch<4;++branch){auto s=Sketch::create_default();auto a=s.add_circle(-12,-8,2),b=s.add_circle(8,5,3);auto [p,q]=candidates({-12,-8},2,{8,5},3)[branch];double length=distance(p,q);P v{(q[0]-p[0])/length,(q[1]-p[1])/length};auto line=s.add_segment(p[0]-2*v[0],p[1]-2*v[1],q[0]+3*v[0],q[1]+3*v[1]);static_cast<void>(s.add_tangent_constraint(line,a));static_cast<void>(s.add_tangent_constraint(line,b));check(s.move_point(s.circles.back().center_point_id,8.4,5.3),"Double T interior contact center drag rejected");solved(s);tangent(s,line,a,false);tangent(s,line,b,false);check(s.constraints.size()==6,"Double T did not retain both interior C contacts");}
    for(bool reverse:{false,true})for(double side:{-1.,1.})for(bool endpoint:{false,true}) {
        auto s=Sketch::create_default();auto circle=Sketch::create_external_reference(ExternalReferenceKind::Edge);circle.source_document_id="external-T";circle.source_owner_id="circle-owner";circle.source_semantic_key="circle:source";
        for(int i=0;i<=64;++i){double angle=i*2*pi/64;circle.cached_points.push_back({-7+2*std::cos(angle),-8+2*std::sin(angle)});}s.add_external_reference(circle);
        const auto line=s.add_segment(endpoint?-7:-12,-8+side*2,10,-8+side*2);
        static_cast<void>(s.add_tangent_constraint(reverse?circle.id:line,reverse?line:circle.id));
        check(std::ranges::count_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.geometry_id==circle.id;})==1,"Explicit external-circle T omitted its C contact");
        const auto verify_external=[&](const Sketch& geometry){
            const auto& l=geometry.segments.front();const auto a=point(geometry,l.first_point_id),b=point(geometry,l.second_point_id);const double length=distance(a,b);
            check(std::abs(std::abs((-7-a[0])*(b[1]-a[1])-(-8-a[1])*(b[0]-a[0]))/length-2)<1e-5,"External-circle independent T equation failed");
            for(const auto& c:geometry.constraints)if(c.kind==ConstraintKind::PointOnCircle){const auto p=point(geometry,c.first_point_id);check(std::abs(distance(p,{-7,-8})-2)<1e-5&&std::abs((p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0]))/length<1e-5,"External-circle independent C contact equations failed");}
            for(const auto& d:geometry.dimensions){double actual=length;if(d.kind==DimensionKind::DistanceX)actual=std::abs(b[0]-a[0]);if(d.kind==DimensionKind::DistanceY)actual=std::abs(b[1]-a[1]);if(d.kind==DimensionKind::Angle){const double angle=d.value*pi/180;check(std::abs(std::abs(((b[0]-a[0])*std::cos(angle)+(b[1]-a[1])*std::sin(angle))/length)-1)<1e-5,"External-circle angle equation failed");}else check(std::abs(actual-std::abs(d.value))<1e-5,"External-circle dimension equation failed");}
        };
        verify_external(s);
        for(const auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle})for(bool locked:{false,true})for(bool driving:{false,true}) {
            auto dimensioned=s;auto d=dimensioned.create_segment_dimension(line,kind);d.locked=locked;d.driving=driving;
            if(locked&&!driving){const auto unchanged=dimensioned.serialized();bool rejected=false;try{dimensioned.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&dimensioned.serialized()==unchanged,"Locked reference dimension was not rejected atomically");continue;}
            dimensioned.apply_dimension(d);
            for(double factor:{1.02,.99,1.01}){const auto before=dimensioned.serialized();check(dimensioned.set_dimension_value(d.id,dimension_display_value(d)*factor),"External-circle T line dimension rejected");if(!driving)check(dimensioned.serialized()==before,"External-circle T reference edit changed its Sketch");solved(dimensioned);verify_external(dimensioned);check(dimensioned.external_references==s.external_references,"External-circle T dimension changed its source");}
        }
        for(int edit=0;edit<3;++edit){const auto before=s.external_references;const auto end=point(s,s.segments.front().second_point_id);check(s.move_point(s.segments.front().second_point_id,end[0]+.2,end[1]+.1*side),"External circle tangent endpoint drag rejected");solved(s);verify_external(s);check(s.external_references==before,"External tangent drag changed its support");const auto& l=s.segments.front();const auto a=point(s,l.first_point_id),b=point(s,l.second_point_id);check(std::abs(std::abs((-7-a[0])*(b[1]-a[1])-(-8-a[1])*(b[0]-a[0]))/distance(a,b)-2)<1e-5,"External-circle T equation failed");for(const auto& c:s.constraints)if(c.kind==ConstraintKind::PointOnCircle){const auto contact=point(s,c.first_point_id);check(std::abs(distance(contact,{-7,-8})-2)<1e-5,"External-circle C incidence failed");}s=Sketch::from_serialized(s.serialized());}
    }
    std::cout<<"Native/external circles refreshed on all branches/orders and unbound double T drags passed\n";
}
void boundary_matrix() {
    for(double separation:{1.1,3.999,4.,4.001,12.})for(bool reverse:{false,true})for(int qa=0;qa<8;++qa)for(int qb=0;qb<8;++qb){auto s=Sketch::create_default();auto a=s.add_circle(0,0,2.5),b=s.add_circle(separation,0,1.5);P ha{2.5*std::cos(qa*pi/4),2.5*std::sin(qa*pi/4)},hb{separation+1.5*std::cos(qb*pi/4),1.5*std::sin(qb*pi/4)};auto line=s.add_common_tangent_segment(reverse?b:a,reverse?hb:ha,reverse?a:b,reverse?ha:hb);tangent(s,line,a,true);tangent(s,line,b,true);}
    auto s=Sketch::create_default();auto a=s.add_circle(-12,-8,2),b=s.add_circle(8,5,3);for(const auto c:std::vector(s.circles)){s.set_point_fixed(c.center_point_id,true);auto d=s.create_circle_radius_dimension(c.id);d.locked=true;s.apply_dimension(d);}auto [p,q]=candidates({-12,-8},2,{8,5},3).front();auto line=s.add_common_tangent_segment(a,p,b,q);auto before=s.serialized();auto end=s.segments.front().first_point_id;check(!s.move_point(end,p[0]+1,p[1]+2)&&s.serialized()==before,"Fully constrained double tangent escaped on drag");bool rejected=false;try{static_cast<void>(s.add_tangent_constraint(line,a));}catch(const std::exception&){rejected=true;}check(rejected&&s.serialized()==before,"Duplicate T partly changed Sketch");
    std::cout<<"640 overlapping/touching/cardinal-click/order cases and fully constrained drag rejection passed\n";
}
void tangent_trim_matrix() {
    for(bool reverse:{false,true})for(double side:{-1.,1.})for(bool endpoint:{false,true}) {
        auto s=Sketch::create_default();auto circle=s.add_circle(0,0,3);
        auto top=s.add_segment(endpoint?0:-10,side*3,10,side*3);
        auto bottom=s.add_segment(-10,-side*3,10,-side*3);
        static_cast<void>(s.add_tangent_constraint(reverse?circle:top,reverse?top:circle));
        static_cast<void>(s.add_tangent_constraint(reverse?circle:bottom,reverse?bottom:circle));
        for(const auto& c:s.constraints)if(c.kind==ConstraintKind::Tangent){check(!c.first_point_id.empty(),"Explicit T has no actual contact point");check(std::ranges::any_of(s.constraints,[&](const auto& incidence){return incidence.kind==ConstraintKind::PointOnCircle&&incidence.geometry_id==circle&&incidence.first_point_id==c.first_point_id;}),"Explicit line/circle T has no C contact");}
        auto pieces=sketch_trim_topology(s,false);check(std::ranges::count_if(pieces,[&](const auto& piece){return piece.geometry_id==circle;})==2,"Explicit T contacts did not split circle into trim pieces");
        auto remove=std::ranges::find_if(pieces,[&](const auto& piece){return piece.geometry_id==circle;});
        static_cast<void>(apply_sketch_trim(s,{*remove}));solved(s);check(s.arcs.size()==1&&s.circles.empty(),"Contact-based circle Trim did not retain an arc");
        check(std::ranges::count_if(s.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;})==2,"Trim lost either tangent relation");
        for(double radius:{3.2,2.8,3.1}){auto d=s.create_arc_radius_dimension(s.arcs.front().id);if(s.dimensions.empty())s.apply_dimension(d);check(s.set_dimension_value(s.dimensions.front().id,radius),"Trimmed C+T arc radius edit rejected");solved(s);s=Sketch::from_serialized(s.serialized());solved(s);}
    }
    std::cout<<"Explicit T adds endpoint/interior C in both orders/sides; Trim and repeated arc radius edits passed\n";
}
Sketch dimensioned_arc_sketch() {
    std::ifstream file("cpp/tests/fixtures/sketch/form-edge-dimensioned-arcs.json");check(bool(file),"Dimensioned FORM-EDGE fixture missing");std::stringstream buffer;buffer<<file.rdbuf();return Sketch::from_serialized(buffer.str());
}
void arc_equations(const Sketch& s){zima::verification::tangency::arc_equations(s);}

void dimensioned_arc_matrix() {
    std::size_t edits{},drags{},rejections{};
    for(double sx:{-1.,1.})for(double sy:{-1.,1.})for(bool external:{false,true})for(bool reversed:{false,true})for(bool locked:{false,true})for(bool diameter:{false,true}) {
        auto original=dimensioned_arc_sketch();for(auto& p:original.points){p.x*=sx;p.y*=sy;}
        for(auto& a:original.arcs){if(sx*sy<0)std::swap(a.start_point_id,a.end_point_id);auto o=point(original,a.center_point_id),p=point(original,a.start_point_id),q=point(original,a.end_point_id);a.start_angle=std::atan2(p[1]-o[1],p[0]-o[0]);a.end_angle=std::atan2(q[1]-o[1],q[0]-o[0]);while(a.end_angle<=a.start_angle)a.end_angle+=2*pi;}
        for(auto& d:original.dimensions){d.locked=locked;if(d.kind==DimensionKind::DistanceX)d.value*=sx;if(d.kind==DimensionKind::DistancePointLine)d.solution_side*=static_cast<int>(sy);if(diameter&&d.kind==DimensionKind::Radius){d.kind=DimensionKind::Diameter;d.value*=2;}}
        for(auto& c:original.constraints)if(reversed){if(c.kind==ConstraintKind::Tangent)std::swap(c.geometry_id,c.second_geometry_id);if(c.kind==ConstraintKind::Symmetric)std::swap(c.first_point_id,c.second_point_id);}
        if(external)for(const auto& axis:{std::string("sketch_axis:x"),std::string("sketch_axis:y")}){auto r=Sketch::create_external_reference(ExternalReferenceKind::Axis);r.source_document_id="external-axis";r.source_owner_id="origin";r.source_semantic_key=axis;r.infinite=true;r.cached_points={P{0,0},axis.back()=='x'?P{1,0}:P{0,1}};original.add_external_reference(r);for(auto& c:original.constraints){if(c.geometry_id==axis)c.geometry_id=r.id;if(c.second_geometry_id==axis)c.second_geometry_id=r.id;}for(auto& d:original.dimensions)if(d.geometry_id==axis)d.geometry_id=r.id;}
        solved(original);arc_equations(original);const auto refs=original.external_references;
        for(const auto d:original.dimensions){auto s=original;for(double factor:{1.05,.96,1.02}){if(!s.set_dimension_value(d.id,dimension_display_value(d)*factor))throw std::runtime_error("Dimensioned FORM-EDGE edit rejected: "+std::to_string(sx)+","+std::to_string(sy)+" external "+std::to_string(external)+" reversed "+std::to_string(reversed)+" locked "+std::to_string(locked)+" diameter "+std::to_string(diameter)+" kind "+std::to_string(static_cast<int>(d.kind))+" factor "+std::to_string(factor));solved(s);arc_equations(s);check(s.external_references==refs,"Dimension edit changed external support");s=Sketch::from_serialized(s.serialized());arc_equations(s);++edits;}}
        for(const auto p:original.points){auto s=original;auto before=s.serialized();bool moved=s.move_point(p.id,p.x+.3*sx,p.y+.2*sy);if(locked){check(!moved&&s.serialized()==before,"Locked FORM-EDGE escaped or changed on an impossible drag");++rejections;}else if(moved){check(distance(point(s,p.id),P{p.x,p.y})>.01,"Unlocked point failed to move");arc_equations(s);check(s.external_references==refs,"Drag changed external support");++drags;}else check(s.serialized()==before,"Rejected drag changed Sketch");}
        auto reference=original;for(auto& d:reference.dimensions){d.driving=false;d.locked=false;}solved(reference);for(const auto d:std::vector(reference.dimensions)){auto before=reference.serialized();check(reference.set_dimension_value(d.id,dimension_display_value(d)*1.2)&&reference.serialized()==before,"Reference edit changed geometry or measured value");}const auto p=reference.points.front();check(reference.move_point(p.id,p.x,p.y+.3*sy),"Underconstrained FORM-EDGE reference-state drag rejected");arc_equations(reference);
        auto invalid=original;auto d=invalid.dimensions.front();auto before=invalid.serialized();check(!invalid.set_dimension_value(d.id,-1)&&invalid.serialized()==before,"Impossible radius edit was not atomic");
    }
    std::cout<<edits<<" dimensioned FORM-EDGE R/D, height and width edits; "<<drags<<" unlocked point drags; "<<rejections<<" locked drag rejections; reflected/native/external/order/reference/reopen equations passed\n";
}
}
int main(int argc,char** argv){try{if(argc>1&&std::string(argv[1])=="--dimensioned-arcs"){dimensioned_arc_matrix();return 0;}user_cases();quadrant_matrix();invalid_cases();edit_matrix();support_matrix();boundary_matrix();tangent_trim_matrix();dimensioned_arc_matrix();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
