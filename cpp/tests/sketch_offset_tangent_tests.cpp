#include <zima/sketcher/curve_geometry.hpp>
#include <zima/sketcher/curve_fillet.hpp>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace zima::sketcher;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void equations(const Sketch& s,const std::string& target,const std::string& offset,double support_radius) {
    const auto& circle=*std::ranges::find(s.circles,target,&SketchCircle::id);
    const auto* center=s.find_point(circle.center_point_id);
    if(std::abs(std::hypot(center->x,center->y)-support_radius-circle.radius)>=1e-6)
        std::cerr<<"Center "<<center->x<<','<<center->y<<" radius "<<circle.radius<<" support "<<support_radius<<" actual "<<s.circular_radius(offset).value_or(-1)<<'\n';
    check(std::abs(std::hypot(center->x,center->y)-support_radius-circle.radius)<1e-6,"External tangent equation failed");
    const auto tangent=std::ranges::find_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::Tangent&&(c.geometry_id==offset||c.second_geometry_id==offset);});
    check(tangent!=s.constraints.end()&&!tangent->first_point_id.empty(),"Missing real tangent contact");
    const auto* contact=s.find_point(tangent->first_point_id);
    check(std::abs(std::hypot(contact->x,contact->y)-support_radius)<1e-6,"Contact left offset");
    check(std::abs(std::hypot(contact->x-center->x,contact->y-center->y)-circle.radius)<1e-6,"Contact left circle");
    check(std::ranges::count_if(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contact->id;})==2,"Contact does not own two C relations");
}
}
int main(int argc,char** argv){try {
    const auto started=std::chrono::steady_clock::now();
    if(argc==2) {
        std::ifstream file(argv[1]);const std::string data((std::istreambuf_iterator<char>(file)),{});
        auto s=Sketch::from_serialized(data);unsigned contacts{},corners{};
        {auto baseline=s;const auto result=baseline.solve();std::cout<<"Supplied baseline status "<<int(result.status)<<" DOF "<<result.remaining_degrees_of_freedom<<" residual "<<result.maximum_residual<<'\n';}
        for(const auto& offset:s.offsets)for(const auto& circle:s.circles) {
            auto candidate=s;
            try {static_cast<void>(candidate.add_tangent_constraint(offset.id,circle.id));++contacts;}
            catch(const std::exception& e){std::cout<<"Supplied support pair rejected: "<<e.what()<<'\n';}
        }
        for(const auto& offset:s.offsets)for(const auto& arc:s.arcs)if(fillet_curve_junction(s,offset.id,arc.id)) {
            for(bool reverse:{false,true})for(double radius:{1.,2.,3.}) {
                auto candidate=s;static_cast<void>(candidate.add_corner_fillet(reverse?arc.id:offset.id,reverse?offset.id:arc.id,radius));
                check(candidate.points==s.points&&candidate.offsets==s.offsets,"Virtual fillet changed authored source");
                static_cast<void>(candidate.evaluated_profile_sketch());
                static_cast<void>(Sketch::from_serialized(candidate.serialized()).evaluated_profile_sketch());++corners;
            }
        }
        std::cout<<"Supplied sketch: "<<contacts<<" tangent pairs and "<<corners<<" virtual fillets passed\n";
        check(contacts>=2&&corners==6,"Supplied offset case not reproduced");return 0;
    }
    for(bool reverse:{false,true})for(bool flipped:{false,true})for(bool diameter:{false,true})for(bool locked:{false,true}) {
        auto s=Sketch::create_default();const auto source=s.add_circle(0,0,10.);
        s.find_point(s.circles.front().center_point_id)->fixed=true;
        const auto source_dimension=s.create_circle_radius_dimension(source);s.apply_dimension(source_dimension);
        const auto offset=s.add_offset(source,2.,flipped);
        const double support_radius=flipped?12.:8.;
        const auto target=s.add_circle(support_radius+3.2,1.,3.);
        const auto tangent=s.add_tangent_constraint(reverse?target:offset,reverse?offset:target);
        equations(s,target,offset,support_radius);
        auto radius=diameter?s.create_circle_diameter_dimension(target):s.create_circle_radius_dimension(target);
        radius.locked=locked;s.apply_dimension(radius);
        for(double value:{3.5,2.5,3.}) {
            check(s.set_dimension_value(radius.id,diameter?value*2:value),"Circle radius edit rejected");
            equations(s,target,offset,support_radius);
        }
        for(double value:{11.,9.,10.}) {
            check(s.set_dimension_value(source_dimension.id,value),"Source radius edit rejected");
            equations(s,target,offset,value+(flipped?2.:-2.));
        }
        for(double value:{2.5,1.5,2.}) {
            s.update_offset(offset,value,flipped);
            equations(s,target,offset,10.+(flipped?value:-value));
        }
        const auto center_id=std::ranges::find(s.circles,target,&SketchCircle::id)->center_point_id;
        for(double angle:{.2,.4,.1}) {
            const auto* before=s.find_point(center_id);const double old_x=before->x,old_y=before->y;
            check(s.move_point(center_id,(support_radius+3.)*std::cos(angle),(support_radius+3.)*std::sin(angle)),"Free center drag rejected");
            check(std::hypot(s.find_point(center_id)->x-old_x,s.find_point(center_id)->y-old_y)>1e-4,"Permitted center drag did not move");
            equations(s,target,offset,support_radius);
        }
        s=Sketch::from_serialized(s.serialized());equations(s,target,offset,support_radius);
        const auto before=s.points.size();s.remove_constraint(tangent);
        check(s.points.size()==before,"Removing T removed contact");
    }
    for(bool reverse:{false,true})for(bool internal:{false,true})for(bool arc:{false,true}) {
        auto s=Sketch::create_default();
        const auto source=arc?s.add_arc(0,0,0,-10,0,10):s.add_circle(0,0,10.);
        const auto center_id=arc?s.arcs.front().center_point_id:s.circles.front().center_point_id;
        s.find_point(center_id)->fixed=true;
        auto size=arc?s.create_arc_radius_dimension(source):s.create_circle_radius_dimension(source);
        size.locked=true;s.apply_dimension(size);
        const auto offset=s.add_offset(source,2,false);
        const auto target=s.add_circle(internal?5.2:10.8,std::sqrt(9.-2.8*2.8),3.);
        auto point=Sketch::create_point(8.,0);const auto contact=point.id;s.points.push_back(std::move(point));
        static_cast<void>(s.add_point_on_circle_constraint(contact,offset));
        static_cast<void>(s.add_point_on_circle_constraint(contact,target));
        const auto count=s.points.size(),constraints=s.constraints.size();
        const auto id=s.add_tangent_constraint(reverse?target:offset,reverse?offset:target);
        check(s.points.size()==count&&s.constraints.size()==constraints+1,"Existing C contact was duplicated");
        const auto& tangent=*std::ranges::find(s.constraints,id,&SketchConstraint::id);
        check(tangent.first_point_id==contact&&tangent.tangent_internal==internal,"Existing C or contact side changed");
        for(double r:{2.5,3.5,3.}) {
            auto candidate=s;auto dimension=candidate.create_circle_radius_dimension(target);dimension.value=r;candidate.apply_dimension(dimension);
            const auto& c=*std::ranges::find(candidate.circles,target,&SketchCircle::id);const auto* p=candidate.find_point(c.center_point_id);
            check(std::abs(std::hypot(p->x,p->y)-(internal?8.-r:8.+r))<1e-6,"Finite arc/internal tangent failed");
            const auto* q=candidate.find_point(contact);check(q->x>=-1e-8&&std::abs(std::hypot(q->x,q->y)-8.)<1e-6,"Contact left finite arc");
        }
    }
    for(bool derived:{false,true})for(bool arcs:{false,true})for(bool reverse:{false,true})for(unsigned order=0;order<6;++order) {
        auto s=Sketch::create_default();std::vector<std::string> supports;
        for(const auto& position:{std::array{-10.,0.},std::array{10.,0.},std::array{0.,std::sqrt(300.)}}) {
            const double radius=derived?6.:5.,angle=std::atan2(std::sqrt(300.)/3-position[1],-position[0]);
            const auto source=arcs?s.add_arc(position[0],position[1],position[0]+radius*std::cos(angle-.9),position[1]+radius*std::sin(angle-.9),
                position[0]+radius*std::cos(angle+.9),position[1]+radius*std::sin(angle+.9)):s.add_circle(position[0],position[1],radius);
            s.find_point(arcs?s.arcs.back().center_point_id:s.circles.back().center_point_id)->fixed=true;
            auto size=arcs?s.create_arc_radius_dimension(source):s.create_circle_radius_dimension(source);size.locked=true;s.apply_dimension(size);
            supports.push_back(derived?s.add_offset(source,1.,false):source);
        }
        const auto target=s.add_circle(.2,5.5,6.4);
        std::array<unsigned,3> sequence{0,1,2};for(unsigned i=0;i<order;++i)std::ranges::next_permutation(sequence);
        for(const auto index:sequence){const auto& support=supports[index];static_cast<void>(s.add_tangent_constraint(reverse?target:support,reverse?support:target));}
        const auto& circle=*std::ranges::find(s.circles,target,&SketchCircle::id);
        const auto* center=s.find_point(circle.center_point_id);
        check(std::hypot(center->x,center->y-std::sqrt(300.)/3)<1e-5,"Three-contact center equation failed");
        check(std::abs(circle.radius-(20/std::sqrt(3.)-5))<1e-5,"Three-contact radius equation failed");
        const auto before=s.serialized();
        const auto center_id=circle.center_point_id;const auto old_center=*center;
        const bool moved=s.move_point(center_id,2,8);
        if(moved||s.serialized()!=before)std::cerr<<"three derived="<<derived<<" reverse="<<reverse<<" moved="<<moved<<" delta="<<std::hypot(s.find_point(center_id)->x-old_center.x,s.find_point(center_id)->y-old_center.y)<<'\n';
        check(!moved&&s.serialized()==before,"Fully constrained circle escaped during drag");
        check(Sketch::from_serialized(before).serialized()==before,"Three-contact round trip changed definition");
    }
    for(bool reverse:{false,true})for(bool trimmed:{false,true}) {
        auto source_sketch=Sketch::create_default();const auto original=source_sketch.add_arc(0,0,0,-10,0,10);
        auto s=Sketch::create_default();auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        reference.source_document_id="external-fixture";reference.source_owner_id="source";reference.source_semantic_key="arc";
        reference.exact_spline=sketch_curve_geometry(source_sketch,original);
        for(unsigned i=0;i<=32;++i){const auto p=zima::kernel::bspline_value(*reference.exact_spline,i/32.);reference.cached_points.push_back({p.x,p.y});}
        s.add_external_reference(reference);
        const auto profile=s.add_external_profile_geometry(reference.id);
        const auto offset=s.add_offset(profile,2,false);
        std::vector<SketchPoint> source_points;
        for(const auto& id:s.import_blocks.front().point_ids)source_points.push_back(*s.find_point(id));
        if(trimmed)static_cast<void>(s.retain_curve_intervals(offset,{{.15,.85}}));
        const auto target=s.add_circle(11.2,1,3);
        static_cast<void>(s.add_tangent_constraint(reverse?target:offset,reverse?offset:target));
        auto radius=s.create_circle_radius_dimension(target);radius.locked=true;s.apply_dimension(radius);
        auto measurement=s.create_circle_diameter_dimension(target);measurement.driving=false;s.apply_dimension(measurement);
        for(double r:{2.5,3.5,3.}) {
            check(s.set_dimension_value(radius.id,r),"External-offset radius edit rejected");equations(s,target,offset,8);
            const auto center_id=std::ranges::find(s.circles,target,&SketchCircle::id)->center_point_id;
            check(s.move_point(center_id,(8+r)*std::cos(.2),(8+r)*std::sin(.2)),"External-offset drag rejected");equations(s,target,offset,8);
            check(s.external_references.front()==reference,"External-offset edit rewrote source");
            for(const auto& p:source_points) {
                auto q=*s.find_point(p.id);
                check(std::hypot(q.x-p.x,q.y-p.y)<1e-12,"External-offset edit moved a projected source point");
                q.x=p.x;q.y=p.y;check(q==p,"External-offset edit changed projected point identity or flags");
            }
            check(std::abs(std::ranges::find(s.dimensions,measurement.id,&SketchDimension::id)->value-2*r)<1e-7,"Reference diameter became stale");
        }
        const auto before=s.serialized();bool rejected{};try{s.update_offset(offset,20,false);}catch(const std::exception&){rejected=true;}
        check(rejected&&s.serialized()==before,"Impossible offset update was not atomic");
        equations(Sketch::from_serialized(before),target,offset,8);
    }
    for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto arc=s.add_arc(-10,0,0,0,-10,10);
        const auto source=s.add_arc(0,-10,11,-10,-11,-10,true);const auto offset=s.add_offset(source,1,false);
        static_cast<void>(s.retain_curve_intervals(offset,{{0,.5}}));
        const auto junction=fillet_curve_junction(s,offset,arc);check(junction.has_value()&&(*junction)[0]!=(*junction)[1],"Explicit trim junction was not retained");
        const auto points=s.points;const auto offsets=s.offsets;
        for(double radius:{1.,2.,3.,1.5}) {
            static_cast<void>(s.add_corner_fillet(reverse?arc:offset,reverse?offset:arc,radius));
            const auto geometry=curve_fillet_geometry(s,offset,arc,radius,(*junction)[0]);
            for(const auto& p:geometry.contacts)check(std::abs(std::hypot(p.x-geometry.center.x,p.y-geometry.center.y)-radius)<1e-8,"Virtual fillet radius changed");
            check(s.points==points&&s.offsets==offsets&&s.corner_radii.size()==1,"Virtual fillet changed source identities");
            static_cast<void>(Sketch::from_serialized(s.serialized()).evaluated_profile_sketch());
        }
    }
    std::cout<<"Offset circular tangency passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
