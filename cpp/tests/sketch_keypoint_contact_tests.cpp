#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include "circle_endpoint_merge_study.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <numbers>
#include <stdexcept>
using namespace zima::sketcher;
namespace {
std::string matrix_case;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Fixture {
    Sketch sketch=Sketch::create_default();
    std::string curve,center,point,segment,key,k,c;
    int kind{},quarter{};
    Fixture(int type,int q,double rotation,bool reverse,bool existing_c,bool free_support):kind(type),quarter(q) {
        const double co=std::cos(rotation),si=std::sin(rotation),hand=reverse?-1.:1.;
        const auto xy=[&](double t){return std::array{8*co*std::cos(t)-4*hand*si*std::sin(t),8*si*std::cos(t)+4*hand*co*std::sin(t)};};
        const double start=.15,end=6.13;
        if(type==0)curve=sketch.add_circle(0,0,8);
        if(type==1)curve=sketch.add_arc(0,0,8*std::cos(start),8*std::sin(start),8*std::cos(end),8*std::sin(end));
        if(type==2)curve=sketch.add_ellipse(0,0,8*co,8*si,-4*hand*si,4*hand*co);
        if(type==3){const auto a=xy(start),b=xy(end);curve=sketch.add_elliptical_arc(0,0,8*co,8*si,-4*hand*si,4*hand*co,a[0],a[1],b[0],b[1],reverse);}
        center=type==0?sketch.circles.front().center_point_id:type==1?sketch.arcs.front().center_point_id:
            type==2?sketch.ellipses.front().center_point_id:sketch.elliptical_arcs.front().center_point_id;
        if(!free_support) {
            for(auto& p:sketch.points)p.fixed=true;
            // Fixed native arc endpoints already determine its radius.
            if(type==0){auto dim=sketch.create_circle_radius_dimension(curve);dim.locked=true;sketch.apply_dimension(dim);}
        }
        const double t=q*std::numbers::pi/2;
        const auto target=type<2?std::array{8*std::cos(t),8*std::sin(t)}:xy(t);
        segment=sketch.add_segment(target[0]+.2,target[1]+.3,target[0]+3,target[1]+5,0,false,false,false);
        point=sketch.segments.back().first_point_id;
        const char* names[]{"circle","arc","ellipse","elliptical_arc"};
        key="sketch_keypoint:"+std::string(names[type])+":"+curve+":"+std::to_string(q);
        if(existing_c)c=sketch.add_point_on_circle_constraint(point,curve);
        k=sketch.add_point_reference_constraint(point,key);
        const auto found=std::ranges::find_if(sketch.constraints,[&](const auto& v){return v.kind==ConstraintKind::PointOnCircle&&v.first_point_id==point&&v.geometry_id==curve&&!v.suppressed;});
        check(found!=sketch.constraints.end(),"Characteristic reference has no C support");
        if(!c.empty())check(found->id==c,"Existing C identity changed");c=found->id;
        check(std::ranges::count_if(sketch.constraints,[&](const auto& v){return v.kind==ConstraintKind::PointOnCircle&&v.first_point_id==point&&v.geometry_id==curve&&!v.suppressed;})==1,"Duplicate C contact");
    }
    void equations(bool exact) {
        const auto result=sketch.solve();check(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-6,"C+K failed to solve");
        const auto* p=sketch.find_point(point);const auto* o=sketch.find_point(center);double a{},b{},r{};
        if(kind<2){a=b=kind==0?sketch.circles.front().radius:sketch.arcs.front().radius;}
        else if(kind==2){const auto& e=sketch.ellipses.front();a=e.major_radius;b=e.minor_radius;r=e.rotation;}
        else {const auto& e=sketch.elliptical_arcs.front();a=e.major_radius;b=e.minor_radius;r=e.rotation;}
        const double x=(p->x-o->x)*std::cos(r)+(p->y-o->y)*std::sin(r),y=-(p->x-o->x)*std::sin(r)+(p->y-o->y)*std::cos(r);
        check(std::abs(x*x/(a*a)+y*y/(b*b)-1)<1e-6,"Point escaped independent conic equation");
        if(exact){
            const double t=quarter*std::numbers::pi/2;
            double tx=o->x+a*std::cos(t),ty=o->y+b*std::sin(t);
            if(kind>=2){const auto major=kind==2?sketch.ellipses.front().major_point_id:sketch.elliptical_arcs.front().major_point_id;
                const auto minor=kind==2?sketch.ellipses.front().minor_point_id:sketch.elliptical_arcs.front().minor_point_id;
                const auto* m=sketch.find_point(major);const auto* n=sketch.find_point(minor);
                tx=o->x+(m->x-o->x)*std::cos(t)+(n->x-o->x)*std::sin(t);
                ty=o->y+(m->y-o->y)*std::cos(t)+(n->y-o->y)*std::sin(t);
            }
            check(std::hypot(p->x-tx,p->y-ty)<1e-6,"K escaped its independent characteristic position");
        }
    }
};
}
int main(){try {
    if(const auto* path=std::getenv("ZIMA_FORM_ARC_DIAGNOSTIC")) {
        std::ifstream input(path);const std::string data{std::istreambuf_iterator<char>(input),{}};
        const auto original=Sketch::from_serialized(data);
        if(std::getenv("ZIMA_FORM_REFERENCE_MOTION")) {
            int cases=0;
            for(bool reference:{false,true})for(bool locked:{false,true}) {
                auto s=original;for(auto d:std::vector(s.dimensions)){d.driving=!reference;d.locked=locked&&!reference;s.apply_dimension(d);}
                const auto ref=s.external_references.front();
                for(double x:{17.5,16.,18.,15.}) {
                    zima::kernel::ViewerReferenceGeometry source;zima::kernel::ViewerPoint p;
                    p.reference={ref.source_owner_id,ref.source_semantic_key,ref.source_instance_path};p.position=s.world_point(x,0);source.points.push_back(p);
                    const auto immutable=source.points;
                    static_cast<void>(s.refresh_external_references(ref.source_document_id,source));
                    check(source.points.size()==immutable.size()&&source.points.front().reference==immutable.front().reference&&source.points.front().position.x==immutable.front().position.x&&source.points.front().position.y==immutable.front().position.y&&source.points.front().position.z==immutable.front().position.z,"FORM refresh mutated source geometry");
                    check(!s.external_references.front().broken&&std::abs(s.external_references.front().cached_points.front()[0]-x)<1e-7,"FORM reference cache did not follow source");
                    const auto k=std::ranges::find_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointReference&&c.second_point_id==ref.id;});check(k!=s.constraints.end(),"FORM refresh lost K identity");
                    const auto* contact=s.find_point(k->first_point_id);check(std::hypot(contact->x-x,contact->y)<1e-6,"FORM native endpoint did not follow external point");
                    const auto equations=[&] {
                        const auto r=s.solve();check(r.status!=SolveStatus::Conflicting&&r.status!=SolveStatus::Invalid&&r.maximum_residual<1e-6,"FORM refreshed constraints conflict");
                        for(const auto& a:s.arcs)for(const auto& id:{a.start_point_id,a.end_point_id}){const auto* p=s.find_point(id);const auto* c=s.find_point(a.center_point_id);check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-a.radius)<1e-6,"FORM refresh detached arc endpoint");}
                        for(const auto& d:s.dimensions)if(d.driving){const auto* p=s.find_point(d.first_point_id);check(std::abs((d.kind==DimensionKind::DistanceSymmetric?2*std::abs(p->x):std::abs(p->y))-d.value)<1e-6,"FORM refresh changed driving dimension");}
                    };
                    equations();if(!reference)for(auto d:std::vector(s.dimensions)){d.value+=.05;s.apply_dimension(d);equations();}
                    const auto* center=s.find_point(s.arcs.front().center_point_id);const auto before=s.serialized();
                    if(!s.move_point(center->id,center->x,center->y-.1))check(s.serialized()==before,"Rejected refreshed FORM drag changed Sketch");
                    equations();s=Sketch::from_serialized(s.serialized());equations();++cases;
                }
            }
            auto frozen=original;for(const auto& p:std::vector(frozen.points))frozen.set_point_fixed(p.id,true);
            const auto before=frozen.serialized();const auto ref=frozen.external_references.front();zima::kernel::ViewerReferenceGeometry source;zima::kernel::ViewerPoint p;
            p.reference={ref.source_owner_id,ref.source_semantic_key,ref.source_instance_path};p.position=frozen.world_point(17.5,0);source.points.push_back(p);
            bool rejected=false;try{static_cast<void>(frozen.refresh_external_references(ref.source_document_id,source));}catch(const std::exception&){rejected=true;}
            check(rejected&&frozen.serialized()==before,"Impossible FORM reference refresh was not atomic");
            std::cout<<"FORM source motion: "<<cases<<" successive refresh/dimension/drag/reopen cases and fixed rejection passed\n";return 0;
        }
        if(std::getenv("ZIMA_FORM_DIMENSION_DIAGNOSTIC")) {
            const auto verify=[&](Sketch& s) {
                const auto r=s.solve();check(r.status!=SolveStatus::Conflicting&&r.status!=SolveStatus::Invalid&&r.maximum_residual<1e-6,"FORM dimension equations failed");
                for(const auto& arc:s.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                    const auto* p=s.find_point(id);const auto* c=s.find_point(arc.center_point_id);
                    check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-6,"FORM dimension detached arc endpoint");
                }
                for(const auto& d:s.dimensions)if(d.driving) {
                    const auto* p=s.find_point(d.first_point_id);
                    check(std::abs((d.kind==DimensionKind::DistanceSymmetric?2*std::abs(p->x):std::abs(p->y))-d.value)<1e-6,"FORM dimension measured value differs");
                }
                check(s.external_references==original.external_references,"FORM dimension changed read-only source");
            };
            int cases=0;
            for(int support=0;support<3;++support)for(bool locked:{false,true})for(bool reference:{false,true}) {
                auto s=original;
                if(support)static_cast<void>(s.add_segment_pair_constraint(support==1?"sketch_axis:y":s.segments.back().id,s.segments.front().id,ConstraintKind::Perpendicular));
                for(auto d:std::vector(s.dimensions)){d.driving=!reference;d.locked=locked&&!reference;s.apply_dimension(d);}verify(s);
                for(int step=0;step<3;++step) {
                    if(!reference)for(auto d:std::vector(s.dimensions)) {d.value+=step==1?-.2:.3;if(step==1)s.apply_dimension(d);else check(s.set_dimension_value(d.id,dimension_display_value(d,s.plane)),"FORM displayed dimension value edit rejected");verify(s);}
                    const auto point=s.arcs.front().center_point_id;const auto* p=s.find_point(point);const auto before=s.serialized();
                    const auto y=p->y;const bool moved=s.move_point(point,p->x,p->y-.1);
                    if(!moved)check(s.serialized()==before,"Rejected FORM dimension drag changed Sketch");
                    else check(std::abs(s.find_point(point)->y-y)>1e-7,"Accepted FORM drag did not move");
                    verify(s);s=Sketch::from_serialized(s.serialized());verify(s);
                }
                ++cases;
            }
            std::cout<<"FORM dimension repeated edit/drag/serialization matrix: "<<cases<<" passed\n";
            {
                auto frozen=original;
                for(const auto& p:std::vector(frozen.points))frozen.set_point_fixed(p.id,true);
                verify(frozen);const auto before=frozen.serialized();const auto* center=frozen.find_point(frozen.arcs.front().center_point_id);
                check(!frozen.move_point(center->id,center->x+1,center->y-1)&&frozen.serialized()==before,"Fully constrained FORM geometry escaped on drag");
            }
            auto solved=original;const auto initial=solved.solve();std::cout<<"INITIAL status="<<int(initial.status)<<" residual="<<initial.maximum_residual<<'\n';
            for(const auto& axis:{std::string("sketch_axis:x"),std::string("sketch_axis:y"),original.segments.back().id}) {
                auto s=original;try{static_cast<void>(s.add_segment_pair_constraint(axis,s.segments.front().id,ConstraintKind::Perpendicular));check(axis!="sketch_axis:x","FORM contradictory perpendicular accepted");std::cout<<"PERP "<<axis<<" OK\n";}catch(const std::exception& e){check(axis=="sketch_axis:x"&&s.serialized()==original.serialized(),"FORM valid perpendicular rejected or rejection mutated Sketch");std::cout<<"PERP "<<axis<<" REJECTED "<<e.what()<<'\n';}
            }
            for(const auto& d:original.dimensions)for(double value:{0.,2.,5.,7.,10.,12.}) {
                auto s=original;auto changed=d;changed.value=value;try{s.apply_dimension(changed);const auto r=s.solve();std::cout<<"DIM "<<d.id<<" value="<<value<<" OK residual="<<r.maximum_residual<<'\n';}catch(const std::exception& e){std::cout<<"DIM "<<d.id<<" value="<<value<<" REJECTED "<<e.what()<<'\n';}
            }
            return 0;
        }
        const auto attempt=[&](const std::string& label,auto operation){auto s=original;try{operation(s);const auto solved=s.solve();std::cout<<label<<": status="<<int(solved.status)<<" residual="<<solved.maximum_residual<<'\n';}catch(const std::exception& e){check(s.serialized()==original.serialized(),"Rejected FORM relation mutated document");std::cout<<label<<": REJECTED "<<e.what()<<'\n';}};
        for(int i=0;i<2;++i)for(const std::string axis:{"sketch_axis:x","sketch_axis:y"})for(bool reverse:{false,true})
            attempt("T arc="+std::to_string(i)+" axis="+axis+" reverse="+std::to_string(reverse),[&](auto& s){static_cast<void>(s.add_tangent_constraint(reverse?axis:s.arcs[i].id,reverse?s.arcs[i].id:axis));});
        for(int i=0;i<2;++i)for(bool start:{false,true})for(bool reverse:{false,true})
            attempt("C arc="+std::to_string(i)+" start="+std::to_string(start)+" reverse="+std::to_string(reverse),[&](auto& s){const auto endpoint=start?s.arcs[i].start_point_id:s.arcs[i].end_point_id;const auto line=s.segments.front().second_point_id;static_cast<void>(s.merge_points(reverse?endpoint:line,reverse?line:endpoint));});
        for(bool reverse:{false,true})attempt("Equal reverse="+std::to_string(reverse),[&](auto& s){static_cast<void>(s.add_equal_radius_constraint(s.arcs[reverse?1:0].id,s.arcs[reverse?0:1].id));});
        for(int kind=0;kind<3;++kind)for(bool reverse:{false,true}) {
            auto s=original;const auto references=s.external_references;
            if(kind==0)static_cast<void>(s.merge_points(reverse?s.arcs.back().start_point_id:s.segments.front().second_point_id,reverse?s.segments.front().second_point_id:s.arcs.back().start_point_id));
            if(kind==1)static_cast<void>(s.add_tangent_constraint(reverse?"sketch_axis:x":s.arcs.front().id,reverse?s.arcs.front().id:"sketch_axis:x"));
            if(kind==2)static_cast<void>(s.add_equal_radius_constraint(s.arcs[reverse?1:0].id,s.arcs[reverse?0:1].id));
            const auto equations=[&] {
                const auto solved=s.solve();check(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting&&solved.maximum_residual<1e-6,"FORM arc relation does not solve");
                for(const auto& arc:s.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                    const auto* p=s.find_point(id);const auto* c=s.find_point(arc.center_point_id);
                    check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-6,"FORM arc endpoint escaped circle equation");
                }
                if(kind==0)check(s.segments.front().second_point_id==s.arcs.back().start_point_id,"FORM C does not share native point");
                if(kind==1){const auto* p=s.find_point(s.arcs.front().start_point_id);const auto* c=s.find_point(s.arcs.front().center_point_id);check(std::abs(p->y)<1e-6&&std::abs(p->x-c->x)<1e-6,"FORM T is not horizontal at its endpoint");}
                if(kind==2)check(std::abs(s.arcs.front().radius-s.arcs.back().radius)<1e-6,"FORM equal radii diverged");
                const auto* origin=s.find_point(s.segments.front().first_point_id);
                const auto* axis_end=s.find_point(s.segments.front().second_point_id);
                check(std::hypot(origin->x,origin->y)<1e-7&&std::abs(axis_end->x)<1e-7,"FORM construction axis escaped its Origin incidence");
                if(kind!=0){const auto* before=original.find_point(original.segments.front().second_point_id);check(std::abs(axis_end->y-before->y)<1e-7,"Unconnected FORM construction axis moved during arc edit");}
                check(s.external_references==references,"FORM edit changed read-only external source packet");
            };
            equations();{
                auto plain=s;const auto* center=plain.find_point(plain.arcs.front().center_point_id);
                const auto x=center->x,y=center->y;const auto id=center->id;
                const bool moved=plain.move_point(id,x+(kind==1?.02:0.),y-.4);
                check(moved,"Undimensioned FORM center rejected its permitted drag");
                if(kind==1)check(std::abs(plain.find_point(id)->x-x)<1e-7,"Axis tangent center escaped its anchored parallel coordinate");
                std::cout<<"FORM undimensioned drag kind="<<kind<<" reverse="<<reverse<<" moved="<<moved<<" from="<<x<<","<<y<<" to="<<plain.find_point(id)->x<<","<<plain.find_point(id)->y<<'\n';
            }
            auto d=s.create_arc_radius_dimension(s.arcs.front().id);s.apply_dimension(d);
            const double radius=s.arcs.front().radius;
            for(double factor:{1.02,.98,1.}) {check(s.set_dimension_value(d.id,radius*factor),"FORM arc relation rejected radius edit");equations();}
            for(int i=0;i<3;++i){const auto* c=s.find_point(s.arcs.front().center_point_id);const auto before=s.serialized();const bool moved=s.move_point(c->id,c->x,c->y-.1);if(!moved)check(s.serialized()==before,"FORM rejected drag mutated document");equations();std::cout<<"FORM drag kind="<<kind<<" reverse="<<reverse<<" moved="<<moved<<'\n';}
            s=Sketch::from_serialized(s.serialized());equations();
        }
        std::cout<<"FORM C/T/equality: both selection orders, independent equations, repeated dimension edits and drag attempts, read-only external packet and reopen passed\n";
        return 0;
    }
    verify_circle_endpoint_merge_study();
    unsigned trim_axis_cases{}, nontrimmable_auxiliary_cases{};
    for(int quarter=0;quarter<4;++quarter)for(bool diameter:{false,true})for(bool locked:{false,true})for(int role:{0,1,2}) {
        auto s=Sketch::create_default();const auto circle=s.add_circle(0,0,15);
        s.set_point_fixed(s.circles.front().center_point_id,true);
        if(role==1)s.set_geometry_construction(circle,true);
        if(role==2)s.set_geometry_centerline(circle,true);
        const auto topology=sketch_trim_topology(s);
        if(role!=0){check(topology.empty(),"Auxiliary/axis circle unexpectedly entered the trim command");++nontrimmable_auxiliary_cases;continue;}
        check(topology.size()==4,"Circle axis trim topology did not expose four quarters");
        static_cast<void>(apply_sketch_trim(s,{topology[quarter]}));
        s.set_point_fixed(s.arcs.front().center_point_id,true);
        auto d=diameter?s.create_arc_diameter_dimension(s.arcs.front().id):s.create_arc_radius_dimension(s.arcs.front().id);d.locked=locked;s.apply_dimension(d);
        const auto equations=[&] {
            const auto solved=s.solve();check(solved.status!=SolveStatus::Conflicting&&solved.status!=SolveStatus::Invalid&&solved.maximum_residual<1e-6,"Axis-trimmed arc does not solve");
            for(const auto& arc:s.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                const auto* p=s.find_point(id);const auto* center=s.find_point(arc.center_point_id);
                check(std::abs(std::hypot(p->x-center->x,p->y-center->y)-arc.radius)<1e-6,"Trim endpoint escaped retained arc");
                check(std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnLine&&c.first_point_id==id&&((c.geometry_id=="sketch_axis:x"&&std::abs(p->y)<1e-7)||(c.geometry_id=="sketch_axis:y"&&std::abs(p->x)<1e-7));}),"Axis trim endpoint lacks its persisted C");
            }
        };
        equations();for(double r:{14.,16.,15.}){check(s.set_dimension_value(d.id,diameter?2*r:r),"Axis-trimmed arc rejected radius/diameter edit");equations();}
        for(int i=0;i<3;++i){const auto before=s.serialized();const auto* p=s.find_point(s.arcs.front().start_point_id);const auto id=p->id;const double x=p->x,y=p->y;const bool moved=s.move_point(id,x*1.01,y*1.01);if(locked)check(s.serialized()==before,"Locked axis trim endpoint escaped by dragging");else check(moved&&std::hypot(s.find_point(id)->x-x,s.find_point(id)->y-y)>1e-5,"Unlocked axis trim endpoint did not move");equations();}
        s=Sketch::from_serialized(s.serialized());equations();++trim_axis_cases;
    }
    std::cout<<"Axis trim matrix: "<<trim_axis_cases<<" ordinary circle quadrants/R/D/lock cases, persisted endpoint C, edits/drags and reopen passed; "<<nontrimmable_auxiliary_cases<<" auxiliary/axis cases correctly excluded from trimming\n";
    unsigned cases{},existing_limitations{};
    for(int kind=0;kind<4;++kind)for(int q=0;q<4;++q) {
        if((kind==1||kind==3)&&q==0)continue; // Outside the intentionally open arc interval.
        for(bool reverse:{false,true})for(bool existing:{false,true})for(bool free:{false,true})for(double rotation:{0.,.37,std::numbers::pi/2}) {
            Fixture f(kind,q,kind>=2?rotation:0,reverse,existing,free);f.equations(true);
            const auto before=f.sketch.serialized();bool rejected=false;
            try{static_cast<void>(f.sketch.add_point_reference_constraint(f.point,f.key));}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Duplicate K changed the document");
            for(int i=0;i<3;++i) {
                const auto* p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.3,p->y-.2));f.equations(true);
                const auto endpoint=f.sketch.segments.back().second_point_id;const auto* e=f.sketch.find_point(endpoint);
                check(f.sketch.move_point(endpoint,e->x+.4,e->y+.2),"Unconstrained endpoint did not move");f.equations(true);
            }
            f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(true);
            const auto with_k=f.sketch;const auto rank_before=f.sketch.solve().remaining_degrees_of_freedom;
            f.sketch.remove_constraint(f.k);f.equations(false);
            check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==f.c&&!c.suppressed;}),"Removing K removed C identity");
            const auto rank_after=f.sketch.solve().remaining_degrees_of_freedom;
            if(rank_after!=rank_before+1)throw std::runtime_error("Removing K rank mismatch: kind="+std::to_string(kind)+" quarter="+std::to_string(q)+" free="+std::to_string(free)+" before="+std::to_string(rank_before)+" after="+std::to_string(rank_after));
            const auto source_points=f.sketch.points;const auto* p=f.sketch.find_point(f.point);const auto start=std::array{p->x,p->y};
            for(int i=0;i<3;++i){p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.5,p->y+.6));f.equations(false);}
            p=f.sketch.find_point(f.point);check(std::hypot(start[0]-p->x,start[1]-p->y)>1e-4,"C-only point did not slide");
            if(!free)for(const auto& source:source_points)if(source.fixed)check(*f.sketch.find_point(source.id)==source,"Sliding contact changed fixed support");
            f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(false);
            f.sketch=with_k;f.sketch.remove_constraint(f.c);f.equations(true); // C and K are independently removable.
            ++cases;
        }
    }
    // Driving/reference, radius/diameter and ellipse axis/rotation dimensions,
    // locked/unlocked editing and repeated value changes of the support.
    for(int kind=0;kind<3;++kind)for(bool locked:{false,true})for(bool reference:{false,true})for(int dim_kind=0;dim_kind<(kind<2?2:3);++dim_kind) {
        Fixture f(kind,1,.37,false,false,true);f.sketch.set_point_fixed(f.center,true);
        auto dim=kind==0?(dim_kind?f.sketch.create_circle_diameter_dimension(f.curve):f.sketch.create_circle_radius_dimension(f.curve)):
            kind==1?(dim_kind?f.sketch.create_arc_diameter_dimension(f.curve):f.sketch.create_arc_radius_dimension(f.curve)):
            dim_kind==2?f.sketch.create_ellipse_rotation_dimension(f.curve):f.sketch.create_ellipse_radius_dimension(f.curve,dim_kind==0);
        dim.locked=locked;dim.driving=!reference;
        if(locked&&reference){const auto before=f.sketch.serialized();bool rejected=false;try{f.sketch.apply_dimension(dim);}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Locked reference dimension changed the document");dim.locked=false;}
        const auto id=dim.id;f.sketch.apply_dimension(dim);f.equations(true);
        if(!reference)for(double multiplier:{1.1,.9,1.05}){check(f.sketch.set_dimension_value(id,dim.value*multiplier),"C+K rejected support dimension edit");f.equations(true);}
        const auto contact=f.c;f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){const auto* p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.4,p->y+.2));f.equations(false);}
        check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==contact;}),"Dimension edit changed C reference identity");++cases;
    }
    for(int kind=0;kind<4;++kind)for(bool locked:{false,true})for(bool reference:{false,true})for(auto dimension_kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle}) {
        Fixture f(kind,2,.37,false,false,false);
        auto dimension=f.sketch.create_segment_dimension(f.segment,dimension_kind);dimension.locked=locked;dimension.driving=!reference;
        if(locked&&reference){const auto before=f.sketch.serialized();bool rejected=false;try{f.sketch.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Locked reference segment dimension changed the document");dimension.locked=false;}
        const auto id=dimension.id;f.sketch.apply_dimension(dimension);f.equations(true);
        if(!reference)for(double delta:{.2,-.1,.3}) {check(f.sketch.set_dimension_value(id,dimension.value+delta),"C+K rejected segment dimension edit");f.equations(true);}
        const auto endpoint=f.sketch.segments.back().second_point_id;
        for(int i=0;i<3;++i){const auto* end=f.sketch.find_point(endpoint);static_cast<void>(f.sketch.move_point(endpoint,end->x+.3,end->y+.2));f.equations(true);}
        f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){const auto* contact=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,contact->x+.3,contact->y+.2));f.equations(false);}
        ++cases;
    }
    for(int kind=0;kind<4;++kind) {
    matrix_case="fully constrained support="+std::to_string(kind);
        Fixture f(kind,2,.37,false,false,false);
        auto length=f.sketch.create_segment_dimension(f.segment,DimensionKind::Distance);length.locked=true;f.sketch.apply_dimension(length);
        auto angle=f.sketch.create_segment_dimension(f.segment,DimensionKind::Angle);angle.locked=true;f.sketch.apply_dimension(angle);
        check(f.sketch.solve().remaining_degrees_of_freedom==0,"Locked segment on fixed support is not fully constrained");
        const auto before=f.sketch.serialized();const auto* end=f.sketch.find_point(f.sketch.segments.back().second_point_id);
        static_cast<void>(f.sketch.move_point(end->id,end->x+2,end->y+2));f.equations(true);
        check(f.sketch.serialized()==before,"Fully constrained C+K segment escaped by dragging");
        auto impossible=f.sketch.create_segment_dimension(f.segment,DimensionKind::Distance);impossible.value+=2;bool rejected=false;
        try{f.sketch.apply_dimension(impossible);}catch(const std::exception&){rejected=true;}
        check(rejected&&f.sketch.serialized()==before,"Impossible dimension changed C+K document");++cases;
    }
    for(int kind=0;kind<4;++kind)for(int dimension_kind=0;dimension_kind<14;++dimension_kind)
        for(bool order:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
        Fixture f(kind,dimension_kind==10?1:2,dimension_kind==10?0:.37,false,false,dimension_kind!=10);const auto end=f.sketch.segments.back().second_point_id;
        if(dimension_kind==10){const auto* p=f.sketch.find_point(f.point);check(f.sketch.move_point(end,p->x,-p->y),"Symmetric fixture could not prepare opposite contact");}
        const auto first=order?end:f.point,second=order?f.point:end;
        matrix_case="support="+std::to_string(kind)+" dimension="+std::to_string(dimension_kind)+" order="+std::to_string(order)+" reference="+std::to_string(reference)+" locked="+std::to_string(locked);
        if(dimension_kind==3){const auto before=f.sketch.serialized();bool rejected=false;
            try{static_cast<void>(f.sketch.create_point_dimension(first,second,DimensionKind::Angle));}catch(const std::invalid_argument&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Unsupported two-point angle changed the document");continue;}
        const auto reference_point=f.sketch.add_point(16,3);f.sketch.set_point_fixed(reference_point,true);
        SketchDimension d;
        if(dimension_kind<4){const DimensionKind kinds[]{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle};d=f.sketch.create_point_dimension(first,second,kinds[dimension_kind]);}
        if(dimension_kind==4)d=f.sketch.create_axis_dimension(end,"sketch_axis:x");
        if(dimension_kind==5)d=f.sketch.create_axis_dimension(end,"sketch_axis:y");
        if(dimension_kind==6)d=f.sketch.create_point_line_dimension(end,"sketch_axis:x");
        if(dimension_kind==7)d=f.sketch.create_point_line_angle_dimension(first,second,"sketch_axis:x");
        if(dimension_kind==8)d=f.sketch.create_three_point_angle_dimension(first,f.center,second);
        if(dimension_kind==9)d=f.sketch.create_four_point_angle_dimension(f.center,reference_point,first,second);
        if(dimension_kind==10)d=f.sketch.create_symmetric_dimension(first,second,"sketch_axis:x");
        if(dimension_kind==11){static_cast<void>(f.sketch.add_segment_constraint(f.segment,ConstraintKind::Horizontal));d=f.sketch.create_line_pair_dimension("sketch_axis:x",f.segment,DimensionKind::DistanceLine);}
        if(dimension_kind==12)d=f.sketch.create_line_symmetric_dimension("sketch_axis:x",f.segment,{},DimensionKind::AngleSymmetric);
        if(dimension_kind==13){static_cast<void>(f.sketch.add_segment_constraint(f.segment,ConstraintKind::Horizontal));d=f.sketch.create_line_symmetric_dimension("sketch_axis:x",f.segment,{},DimensionKind::DistanceLineSymmetric);}
        d.driving=!reference;d.locked=locked&&!reference;const auto id=d.id;f.sketch.apply_dimension(d);f.equations(true);
        const auto verify_dimension=[&] {
            const auto& actual=*std::ranges::find(f.sketch.dimensions,id,&SketchDimension::id);
            const auto position=[&](const std::string& key){const auto* p=f.sketch.find_point(key);check(p,"Independent dimension point missing");return std::array{p->x,p->y};};
            const auto a=position(first),b=position(second),o=position(f.center);
            const auto angle=[](std::array<double,2> u,std::array<double,2> v){return std::acos(std::clamp((u[0]*v[0]+u[1]*v[1])/(std::hypot(u[0],u[1])*std::hypot(v[0],v[1])),-1.,1.))*180/std::numbers::pi;};
            const std::array direction{b[0]-a[0],b[1]-a[1]};double measured{};
            if(dimension_kind==0)measured=std::hypot(direction[0],direction[1]);
            if(dimension_kind==1)measured=direction[0];
            if(dimension_kind==2)measured=direction[1];
            if(dimension_kind==4)measured=position(end)[1];
            if(dimension_kind==5)measured=position(end)[0];
            if(dimension_kind==6)measured=std::abs(position(end)[1]);
            if(dimension_kind==7)measured=angle({1,0},direction);
            if(dimension_kind==8)measured=angle({a[0]-o[0],a[1]-o[1]},{b[0]-o[0],b[1]-o[1]});
            if(dimension_kind==9){const auto r=position(reference_point);measured=angle({r[0]-o[0],r[1]-o[1]},direction);}
            if(dimension_kind==10){measured=std::abs(a[1])+std::abs(b[1]);if(actual.driving)check(std::abs(std::abs(a[1])-std::abs(b[1]))<1e-6,"Independent symmetric point equation failed");}
            if(dimension_kind>=11){const auto segment=std::ranges::find(f.sketch.segments,f.segment,&SketchSegment::id);const auto p=position(segment->first_point_id),q=position(segment->second_point_id);
                if(dimension_kind==12){const double theta=angle({1,0},{q[0]-p[0],q[1]-p[1]});measured=2*std::min(theta,180-theta);}
                else {measured=std::abs(p[1])*(dimension_kind==13?2:1);if(actual.driving)check(std::abs(p[1]-q[1])<1e-6,"Independent parallel line equation failed");}}
            check(std::abs(std::abs(measured)-std::abs(actual.value))<1e-6,"Independent dimension equation failed");
        };verify_dimension();
        if(!reference)for(double delta:{.2,-.1,.3}){
            matrix_case+=" value="+std::to_string(d.value+delta);
            const auto before=f.sketch.serialized();
            if(!f.sketch.set_dimension_value(id,d.value+delta)) {
                check(dimension_kind==10||dimension_kind==11||dimension_kind==13,"C+K rejected point/contact dimension edit");
                check(f.sketch.serialized()==before,"Rejected contact dimension changed the document");
                auto k_only=f.sketch;k_only.remove_constraint(f.c);const auto baseline=k_only.serialized();
                check(!k_only.set_dimension_value(id,d.value+delta)&&k_only.serialized()==baseline,"C+K introduced a dimension rejection absent from K-only support");
                ++existing_limitations;
            }
            f.equations(true);
            verify_dimension();
        }
        for(int i=0;i<3;++i){matrix_case+=" endpoint-drag";const auto* endpoint=f.sketch.find_point(end);static_cast<void>(f.sketch.move_point(end,endpoint->x+.2,endpoint->y+.3));f.equations(true);verify_dimension();}
        f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){matrix_case+=" contact-drag";const auto* endpoint=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,endpoint->x+.2,endpoint->y+.3));f.equations(false);verify_dimension();}
        f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(false);verify_dimension();++cases;
    }
    for(int kind=0;kind<4;++kind)for(bool remove_k:{false,true}) {
        Fixture f(kind,2,0,false,false,true);if(remove_k)f.sketch.remove_constraint(f.k);
        const auto pieces=sketch_trim_topology(f.sketch);std::optional<SketchTrimPiece> removed;
        for(const auto& piece:pieces)if(piece.geometry_id==f.curve&&!piece.points.empty()&&piece.points[piece.points.size()/2][0]>0) {removed=piece;break;}
        check(removed.has_value(),"Trim contact fixture has no opposite curve piece");
        const auto mapping=apply_sketch_trim(f.sketch,{*removed});
        check(mapping.geometry_mapping.contains(f.curve),"Trim did not remap the supporting curve");
        const auto contact=std::ranges::find_if(f.sketch.constraints,[&](const auto& c){return c.id==f.c;});
        check(contact!=f.sketch.constraints.end()&&contact->first_point_id==f.point&&contact->kind==ConstraintKind::PointOnCircle,"Trim lost independent C identity");
        check(std::ranges::count_if(f.sketch.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==f.point;})==1,"Trim converted K into a duplicate C");
        const auto result=f.sketch.solve();check(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-6,"Trimmed C+K contact does not solve");
        const auto* point=f.sketch.find_point(f.point);check(point,"Trim lost the attached native point");
        f.sketch=Sketch::from_serialized(f.sketch.serialized());check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==f.c;}),"Trim persistence lost C identity");++cases;
    }
    // Projected external conics are native analytic supports but remain read-only.
    for(int kind=0;kind<4;++kind) {
    matrix_case="external support="+std::to_string(kind);
    auto external=Sketch::create_default();auto source=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    source.source_document_id="external-document";source.source_owner_id="external-curve";source.source_semantic_key="conic";
    const double b=kind<2?8:4,start=(kind==1||kind==3)?.15:0,end=(kind==1||kind==3)?6.13:2*std::numbers::pi;
    for(int i=0;i<=32;++i){const double t=start+(end-start)*i/32;source.cached_points.push_back({8*std::cos(t),b*std::sin(t)});}
    external.add_external_reference(source);const auto support=external.add_external_profile_geometry(source.id);
    const auto source_points=external.points;const auto contact=external.add_point(-7,1);
    const char* names[]{"circle","arc","ellipse","elliptical_arc"};
    if(kind==1||kind==3){
        check(std::ranges::any_of(external.bsplines,[&](const auto& curve){return curve.id==support;}),"Open external conic profile changed representation");
        const auto before=external.serialized();bool rejected=false;
        try{static_cast<void>(external.add_point_reference_constraint(contact,"sketch_keypoint:"+std::string(names[kind])+":"+support+":2"));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&external.serialized()==before,"Nonanalytic external profile accepted an invalid conic K");++cases;continue;
    }
    const auto anchor=external.add_point_reference_constraint(contact,"sketch_keypoint:"+std::string(names[kind])+":"+support+":2");
    external.remove_constraint(anchor);
    for(int i=0;i<3;++i){const auto* current=external.find_point(contact);check(external.move_point(contact,current->x+.4,current->y+.6),"External C-only contact did not move");
        current=external.find_point(contact);check(std::abs(current->x*current->x/64+current->y*current->y/(b*b)-1)<1e-6,"External C-only contact escaped conic");
        for(const auto& p:source_points)check(*external.find_point(p.id)==p,"Contact drag modified read-only projected source");
        check(external.external_references.front()==source,"Contact drag rewrote external reference packet");}
    ++cases;
    }
    auto origin=Sketch::create_default();const auto p=origin.add_point(1,2);static_cast<void>(origin.add_point_reference_constraint(p,"sketch_origin"));
    check(origin.constraints.size()==1&&origin.constraints.front().kind==ConstraintKind::PointReference,"Origin K acquired a curve contact");
    std::cout<<"C+K native matrix: "<<cases<<" cases, independent conic equations, successive drags and value edits, fixed/free support, C reuse, independent removal and persistence passed; "<<existing_limitations<<" existing K-only dimension edit limitations rejected atomically\n";return 0;
}catch(const std::exception& e){std::cerr<<matrix_case<<": "<<e.what()<<'\n';return 1;}}
