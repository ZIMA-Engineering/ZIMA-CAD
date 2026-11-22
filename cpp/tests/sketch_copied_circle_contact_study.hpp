#pragma once
#include "double_circle_tangent_study.hpp"
#include <zima/sketcher/curve_geometry.hpp>

namespace zima::test {
inline sketcher::Sketch make_copied_circle_contact_fixture(double mirror,double angle,bool internal_first=false,bool internal_second=false) {
    using namespace sketcher;
    auto s=Sketch::create_default();std::array<std::string,2> native,external;
    const auto transform=[&](double x,double y){return std::array{100.+x*std::cos(angle)-mirror*y*std::sin(angle),30.+x*std::sin(angle)+mirror*y*std::cos(angle)};};
    for(unsigned i=0;i<2;++i) {
        const auto c=transform(i?60.:0.,i?0.:50.);
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        ref.source_document_id="circular-source";ref.source_owner_id="round-support";
        ref.source_semantic_key="circle:"+std::to_string(i);
        for(unsigned j=0;j<=64;++j){const double a=2*std::numbers::pi*j/64;ref.cached_points.push_back({c[0]+10*std::cos(a),c[1]+10*std::sin(a)});}
        auto source=Sketch::create_default();const auto source_circle=source.add_circle(c[0],c[1],10.);
        ref.exact_spline=sketch_curve_geometry(source,source_circle);
        external[i]=ref.id;s.add_external_reference(ref);
        native[i]=s.add_circle(c[0],c[1],10.);
        const auto center=s.circles.back().center_point_id;
        static_cast<void>(s.add_point_reference_constraint(center,"sketch_external_center:"+external[i]));
        static_cast<void>(s.add_equal_radius_constraint(external[i],native[i]));
    }
    const double end_angle=(internal_second?3.:1.)*std::numbers::pi/2+.2;
    const double px=60.+10*std::cos(end_angle),py=10*std::sin(end_angle);
    const double dx=px,dy=py-50.;
    const double offset=internal_first?-10.:10.;
    const double radius=(dx*dx+dy*dy-2*offset*dx+100)/(2*(dx-offset));
    const auto center=transform(offset+radius,50),start=transform(offset,50),end=transform(px,py);
    const auto driven=s.add_arc(center[0],center[1],start[0],start[1],end[0],end[1],false,1e-6,mirror<0);
    const auto arc=s.arcs.back();
    const auto first=mirror<0?arc.end_point_id:arc.start_point_id;
    const auto contact=mirror<0?arc.start_point_id:arc.end_point_id;
    static_cast<void>(s.add_point_on_circle_constraint(first,native[0],true));
    static_cast<void>(s.add_tangent_constraint(native[0],driven,first));
    static_cast<void>(s.add_point_on_circle_constraint(contact,external[1],true));
    return s;
}
inline int run_copied_circle_contact_study() {
    using namespace sketcher;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    unsigned cases{},failures{};
    for(bool reverse:{false,true})for(double mirror:{-1.,1.})for(double angle:{0.,.61})
    for(bool external_target:{false,true})for(bool internal_first:{false,true})for(bool internal_second:{false,true}) {
        ++cases;std::string stage="fixture";
        try {
            auto s=make_copied_circle_contact_fixture(mirror,angle,internal_first,internal_second);
            const std::array native{s.circles[0].id,s.circles[1].id};
            const std::array external{s.external_references[0].id,s.external_references[1].id};
            const auto arc=s.arcs.back();const auto driven=arc.id;
            const auto first=mirror<0?arc.end_point_id:arc.start_point_id;
            const auto contact=mirror<0?arc.start_point_id:arc.end_point_id;
            const auto transform=[&](double x,double y){return std::array{100.+x*std::cos(angle)-mirror*y*std::sin(angle),30.+x*std::sin(angle)+mirror*y*std::cos(angle)};};
            const auto references=s.external_references;
            const auto verify=[&](const Sketch& value) {
                verify_circle_contacts(value);
                check(value.external_references==references,"External circular source changed");
                for(unsigned i=0;i<2;++i) {
                    const auto c=std::ranges::find(value.circles,native[i],&SketchCircle::id);
                    const auto* p=value.find_point(c->center_point_id);const auto expected=transform(i?60.:0.,i?0.:50.);
                    check(std::hypot(p->x-expected[0],p->y-expected[1])<1e-7&&std::abs(c->radius-10)<1e-7,"Native reference copy moved");
                }
                check(value.find_point(first)&&value.find_point(contact),"Contact point identity lost");
            };
            stage="second tangent";
            const auto support=external_target?external[1]:native[1];
            auto blocked=s;for(auto& p:blocked.points)p.fixed=true;
            const auto blocked_snapshot=blocked.serialized();bool impossible_rejected=false;
            try{static_cast<void>(blocked.add_tangent_constraint(support,driven));}catch(const std::exception&){impossible_rejected=true;}
            check(impossible_rejected&&blocked.serialized()==blocked_snapshot,"Impossible tangent left a partial contact");
            if(!external_target) {
                auto direct=s;static_cast<void>(direct.add_point_on_circle_constraint(contact,support,true));
                static_cast<void>(direct.add_tangent_constraint(reverse?driven:support,reverse?support:driven));verify(direct);
            }
            const auto point_count=s.points.size();
            const auto tangent=s.add_tangent_constraint(reverse?driven:support,reverse?support:driven);
            verify(s);check(s.points.size()==point_count,"Second tangency duplicated the arc endpoint");
            check(std::ranges::find(s.constraints,tangent,&SketchConstraint::id)->tangent_internal==internal_second,"Second tangent changed contact side");
            check(std::ranges::find(s.constraints,tangent,&SketchConstraint::id)->first_point_id==contact,"Second tangent selected a different contact");
            stage="dimensions and dragging";
            for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
                auto edited=s;auto dim=diameter?edited.create_arc_diameter_dimension(driven):edited.create_arc_radius_dimension(driven);
                dim.driving=!reference;dim.locked=locked;
                if(reference&&locked) {
                    const auto before=edited.serialized();bool rejected=false;
                    try{edited.apply_dimension(dim);}catch(const std::exception&){rejected=true;}
                    check(rejected&&edited.serialized()==before,"Locked reference dimension changed the sketch");continue;
                }
                edited.apply_dimension(dim);verify(edited);
                if(!reference)for(double factor:{1.01,.99,1.02}) {
                    check(edited.set_dimension_value(dim.id,dim.value*factor),"Successive arc size edit failed");verify(edited);
                    check(std::abs(*edited.circular_radius(driven)-dim.value*factor/(diameter?2:1))<1e-7,"Arc size missed its target");
                }
                for(const auto& id:{arc.center_point_id,first,contact})for(unsigned repeat=0;repeat<3;++repeat) {
                    const auto before=edited.serialized();const auto q=*edited.find_point(id);
                    const auto moved=edited.move_point(id,q.x+.11,q.y+.17);
                    if(!reference&&locked)check(!moved&&before==edited.serialized(),"Fully constrained arc escaped on drag");
                    else {check(moved,"Free arc handle cannot be dragged");const auto* p=edited.find_point(id);check(std::hypot(p->x-q.x,p->y-q.y)>1e-6,"Arc drag did not move");}
                    verify(edited);
                }
                const auto saved=edited.serialized();edited=Sketch::from_serialized(saved);
                check(edited.serialized()==saved,"Sketch reopen changed contact identities");verify(edited);
            }
            stage="positional and angular dimensions";
            for(unsigned kind=0;kind<4;++kind)for(bool order:{false,true}) {
                const auto measure=[&](const Sketch& value) {
                    if(kind==3)return value.create_three_point_angle_dimension(order?contact:first,arc.center_point_id,order?first:contact);
                    return value.create_point_dimension(order?arc.center_point_id:"sketch_origin",order?"sketch_origin":arc.center_point_id,
                        kind==0?DimensionKind::Distance:kind==1?DimensionKind::DistanceX:DimensionKind::DistanceY);
                };
                std::vector<double> targets;
                for(double factor:{1.01,.99,1.02}) {
                    auto expected=s;auto size=expected.create_arc_radius_dimension(driven);size.value*=factor;expected.apply_dimension(size);targets.push_back(measure(expected).value);
                }
                for(bool reference:{false,true})for(bool locked:{false,true}) {
                    if(reference&&locked)continue;
                    auto edited=s;auto dim=measure(edited);dim.driving=!reference;dim.locked=locked;
                    const bool constant=std::ranges::all_of(targets,[&](double v){return std::abs(v-dim.value)<1e-7;});
                    const auto before=edited.serialized();bool redundant=false;
                    try{edited.apply_dimension(dim);}catch(const RedundantConstraint&){redundant=true;}
                    if(constant&&!reference){check(redundant&&before==edited.serialized(),"Constant positional dimension changed geometry");continue;}
                    check(!redundant,"Movable dimension rejected as redundant");verify(edited);
                    if(!reference)for(double target:targets) {
                        stage="dimension kind="+std::to_string(kind)+" order="+std::to_string(order)+" locked="+std::to_string(locked)+" target="+std::to_string(target);
                        auto update=dim;update.value=target;edited.apply_dimension(update);verify(edited);
                        check(std::abs(measure(edited).value-target)<1e-6,"Positional/angular dimension missed target");
                    }
                    for(unsigned repeat=0;repeat<3;++repeat) {
                        const auto q=*edited.find_point(arc.center_point_id);const auto snapshot=edited.serialized();
                        const bool moved=edited.move_point(q.id,q.x+.1,q.y+.2);
                        if(!reference&&locked)check(!moved&&snapshot==edited.serialized(),"Locked positional/angular dimension escaped");
                        else check(moved&&std::hypot(edited.find_point(q.id)->x-q.x,edited.find_point(q.id)->y-q.y)>1e-6,"Free positional/angular geometry cannot move");
                        verify(edited);
                    }
                }
            }
            stage="atomic duplicate and removal";
            const auto before=s.serialized();bool rejected=false;
            try{static_cast<void>(s.add_tangent_constraint(driven,support));}catch(const std::exception&){rejected=true;}
            check(rejected&&s.serialized()==before,"Duplicate T was not rejected atomically");
            s.remove_constraint(tangent);check(s.find_point(contact),"Removing T deleted the contact point");
            check(std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contact&&c.geometry_id==external[1];}),"Removing T deleted external C");
            if(!external_target)check(std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==contact&&c.geometry_id==native[1];}),"Removing T deleted native C");
        }catch(const std::exception& e){++failures;std::cout<<"Copied circle reverse="<<reverse<<" mirror="<<mirror<<" angle="<<angle<<" external="<<external_target<<" internal="<<internal_first<<internal_second<<" stage="<<stage<<": "<<e.what()<<'\n';}
    }
    if(const auto* path=std::getenv("ZIMA_COPIED_CIRCLE_SKETCH"))for(bool reverse:{false,true}) {
        ++cases;try{
            std::ifstream file(path);check(bool(file),"Saved sketch fixture missing");
            auto s=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(file),{}));
            const auto refs=s.external_references;const auto arc=s.arcs.front().id,circle=s.circles.at(2).id;
            static_cast<void>(s.add_tangent_constraint(reverse?arc:circle,reverse?circle:arc));
            verify_circle_contacts(s);check(s.external_references==refs,"Saved sketch external supports changed");
        }catch(const std::exception& e){++failures;std::cout<<"Saved copied circle reverse="<<reverse<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Copied circular contact matrix: "<<cases<<" cases, "<<failures<<" failures\n";
    return failures?1:0;
}
}
