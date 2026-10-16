#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void verify_circle_endpoint_merge_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    unsigned cases{}, rejected_free_drags{};
    for(bool reverse:{false,true})for(bool construction:{false,true})for(bool vertical:{false,true})
        for(double side:{-1.,1.})for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
        auto s=Sketch::create_default();
        const auto xy=[&](double x,double y){return vertical?std::array{y,x}:std::array{x,y};};
        const auto circle=s.add_circle(0,0,15);const auto center=s.circles.front().center_point_id;
        s.set_point_fixed(center,true);
        const auto contact_position=xy(side*15,0);
        const auto contact=s.add_point(contact_position[0],contact_position[1]);
        const auto c=s.add_point_on_circle_constraint(contact,circle);
        const std::string axis=vertical?"sketch_axis:y":"sketch_axis:x";
        static_cast<void>(s.add_point_on_line_constraint(contact,axis));
        auto d=diameter?s.create_circle_diameter_dimension(circle):s.create_circle_radius_dimension(circle);
        d.driving=!reference;d.locked=locked;const auto dimension=d.id;
        if(reference&&locked) {
            const auto before=s.serialized();bool rejected=false;
            try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}
            check(rejected&&s.serialized()==before,"Locked reference dimension changed endpoint merge fixture");d.locked=false;
        }
        s.apply_dimension(d);
        const auto a=xy(-side*10,0),b=xy(side*23,0);
        static_cast<void>(s.add_segment(a[0],a[1],b[0],b[1],0,construction));const auto line=s.segments.back();
        s.set_point_fixed(line.first_point_id,true);
        static_cast<void>(s.add_point_on_line_constraint(line.second_point_id,axis));
        const auto survivor=s.merge_points(reverse?contact:line.second_point_id,reverse?line.second_point_id:contact);
        const auto equations=[&] {
            const auto solved=s.solve();check(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting&&solved.maximum_residual<1e-6,"Merged circle endpoint does not solve");
            const auto* p=s.find_point(survivor);const auto* o=s.find_point(center);const auto radius=s.circles.front().radius;
            check(p&&std::abs(vertical?p->x:p->y)<1e-7,"Merged endpoint escaped its axis");
            check(std::abs(std::hypot(p->x-o->x,p->y-o->y)-radius)<1e-7,"Merged endpoint escaped circle C");
            check(o->x==0&&o->y==0,"Endpoint merge moved fixed circle center");
            check(s.segments.back().id==line.id&&s.segments.back().second_point_id==survivor,"Endpoint merge changed segment identity or incidence");
            check(std::ranges::any_of(s.constraints,[&](const auto& value){return value.id==c&&value.first_point_id==survivor&&value.geometry_id==circle;}),"Endpoint merge replaced original circle C");
        };
        equations();const auto* merged=s.find_point(survivor);
        check(std::hypot(merged->x-contact_position[0],merged->y-contact_position[1])<1e-7,"Endpoint merge moved the circle contact instead of the free endpoint");
        if(!reference)for(double radius:{14.,16.,15.}){check(s.set_dimension_value(dimension,diameter?2*radius:radius),"Merged endpoint rejected R/D edit");equations();}
        for(int i=0;i<3;++i) {
            const auto* p=s.find_point(survivor);const auto before=s.serialized();const auto target=xy((vertical?p->y:p->x)+side,0);
            const bool moved=s.move_point(survivor,target[0],target[1]);equations();
            if(reference) { if(!moved) {check(s.serialized()==before,"Rejected free drag changed document");++rejected_free_drags;} else check(s.serialized()!=before,"Accepted free drag did not move"); }
            else check(s.serialized()==before,"Fully constrained merged endpoint escaped by dragging");
        }
        s=Sketch::from_serialized(s.serialized());equations();++cases;
    }
    std::cout<<"Free contact drag limitations: "<<rejected_free_drags<<" rejected atomically\n";
    std::cout<<"Circle endpoint merge: "<<cases<<" order/orientation/side/construction/R/D/reference/lock cases, successive edits/drags, fixed source, C identity and persistence passed\n";
    unsigned movement_cases{};
    for(bool vertical:{false,true})for(bool fixed_center:{false,true})for(bool fixed_line:{false,true}) {
        auto base=Sketch::create_default();
        const auto circle=base.add_circle(0,0,15),center=base.circles.front().center_point_id;
        const auto contact=base.add_point(vertical?0:15,vertical?15:0);
        static_cast<void>(base.add_point_on_circle_constraint(contact,circle));
        static_cast<void>(base.add_segment(vertical?0:-10,vertical?-10:0,vertical?0:23,vertical?23:0,0,true));
        const auto line=base.segments.back();base.set_segment_centerline(line.id,true);
        base.set_point_fixed(center,fixed_center);base.set_point_fixed(line.first_point_id,fixed_line);
        base.apply_dimension(base.create_circle_radius_dimension(circle));
        auto original_path=base,endpoint_first=base;
        // Contact-first bypasses the new seed branch and retains the prior merge path.
        static_cast<void>(original_path.merge_points(contact,line.second_point_id));
        static_cast<void>(endpoint_first.merge_points(line.second_point_id,contact));
        for(int i=0;i<3;++i) {
            const auto before=endpoint_first.serialized();
            const auto* p=endpoint_first.find_point(center);
            const double x=p->x+1,y=p->y+2;
            const bool old_moved=original_path.move_point(center,x,y),moved=endpoint_first.move_point(center,x,y);
            check(old_moved==moved,"Endpoint merge changed connected center-drag acceptance");
            check(moved==(!fixed_center&&!fixed_line),"Connected circle ignored anchored geometry or failed free translation");
            if(!moved)check(endpoint_first.serialized()==before,"Rejected center drag changed document");
            for(const auto& id:{center,line.first_point_id}) {
                const auto* a=original_path.find_point(id);const auto* b=endpoint_first.find_point(id);
                check(std::hypot(a->x-b->x,a->y-b->y)<1e-7,"Endpoint merge changed existing connected-axis movement");
            }
            const auto* a=original_path.find_point(contact);const auto* b=endpoint_first.find_point(line.second_point_id);
            check(std::hypot(a->x-b->x,a->y-b->y)<1e-7,"Merge order changed connected endpoint movement");
            const auto mesh=endpoint_first.viewer_mesh();
            for(const auto& axis:mesh.axes)if(axis.reference.semantic_key.starts_with("sketch_axis:"))
                check(std::hypot(axis.point.x,axis.point.y,axis.point.z)<1e-12,"Circle movement changed built-in axis origin");
            check(endpoint_first.solve().maximum_residual<1e-6,"Connected center drag violated constraints");
        }
        ++movement_cases;
    }
    std::cout<<"Connected construction-axis audit: "<<movement_cases<<" fixed/free/orientation cases, three center drags, unchanged built-in axes and equivalent prior merge path passed\n";
    unsigned tangent_drag_cases{};
    for(bool vertical:{false,true})for(double side:{-1.,1.})for(bool reverse:{false,true})
        for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
        auto s=Sketch::create_default();const auto xy=[&](double x,double y){return vertical?std::array{-y,x}:std::array{x,y};};
        const auto c=xy(3,side*10),p=xy(3,0),end=xy(13,side*10);
        const auto arc=s.add_arc(c[0],c[1],p[0],p[1],end[0],end[1]);const auto center=s.arcs.front().center_point_id,contact=s.arcs.front().start_point_id;
        const std::string axis=vertical?"sketch_axis:y":"sketch_axis:x";
        static_cast<void>(s.add_point_on_line_constraint(contact,axis));s.set_point_fixed(contact,true);
        static_cast<void>(s.add_tangent_constraint(reverse?arc:axis,reverse?axis:arc));
        auto d=diameter?s.create_arc_diameter_dimension(arc):s.create_arc_radius_dimension(arc);d.driving=!reference;d.locked=locked;
        if(reference&&locked){const auto before=s.serialized();bool rejected{};try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&s.serialized()==before,"Locked reference arc dimension was not rejected atomically");d.locked=false;}
        s.apply_dimension(d);
        const auto equations=[&] {
            const auto solved=s.solve();check(solved.status!=SolveStatus::Conflicting&&solved.status!=SolveStatus::Invalid&&solved.maximum_residual<1e-6,"Axis tangent drag does not solve");
            const auto* o=s.find_point(center);const auto* q=s.find_point(contact);
            check(std::abs(vertical?o->y-q->y:o->x-q->x)<1e-7,"Axis tangent lost anchored parallel coordinate");
            check(std::abs(std::hypot(o->x-q->x,o->y-q->y)-s.arcs.front().radius)<1e-7,"Axis tangent contact escaped circular equation");
            check(std::hypot(q->x-p[0],q->y-p[1])<1e-7,"Axis tangent drag moved its fixed contact");
            check(side*(vertical?q->x-o->x:o->y-q->y)>0,"Axis tangent edit switched contact side");
        };
        if(!reference)for(double radius:{14.,16.,15.}){check(s.set_dimension_value(d.id,diameter?2*radius:radius),"Axis tangent rejected R/D value edit");equations();}
        for(int i=0;i<3;++i) {
            const auto* o=s.find_point(center);const double x=o->x,y=o->y;
            const auto delta=xy(.02,side*.4);const auto before=s.serialized();const bool moved=s.move_point(center,x+delta[0],y+delta[1]);
            if(!reference&&locked)check(!moved&&s.serialized()==before,"Locked tangent radius escaped by dragging");
            else check(moved&&std::hypot(s.find_point(center)->x-x,s.find_point(center)->y-y)>1e-5,"Free tangent radius did not follow permitted mouse direction");
            equations();
        }
        s=Sketch::from_serialized(s.serialized());equations();++tangent_drag_cases;
    }
    std::cout<<"Axis tangent center drag: "<<tangent_drag_cases<<" axis/side/order/R/D/reference/lock cases, transverse cursor projection, value edits and reopen passed\n";
}
