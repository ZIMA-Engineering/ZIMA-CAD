#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>

namespace trimmed_rectangle_study {
using namespace zima::sketcher;
inline void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
inline void equations(const Sketch& s) {
    for(const auto& a:s.arcs) {
        const auto* c=s.find_point(a.center_point_id);
        for(const auto& id:{a.start_point_id,a.end_point_id}) {
            const auto* p=s.find_point(id);
            check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-a.radius)<1e-6,"Trim arc incidence failed");
        }
    }
    for(const auto& c:s.constraints) {
        if(c.suppressed)continue;
        const auto* p=s.find_point(c.first_point_id);const auto* q=s.find_point(c.second_point_id);
        if(c.kind==ConstraintKind::Horizontal)check(p&&q&&std::abs(p->y-q->y)<1e-6,"Rectangle H failed");
        if(c.kind==ConstraintKind::Vertical)check(p&&q&&std::abs(p->x-q->x)<1e-6,"Rectangle V failed");
        if(c.kind==ConstraintKind::PointOnLine) {
            if(c.geometry_id=="sketch_axis:x")check(std::abs(p->y)<1e-6,"Origin X support failed");
            else if(c.geometry_id=="sketch_axis:y")check(std::abs(p->x)<1e-6,"Origin Y support failed");
            else {
                const auto l=std::ranges::find(s.segments,c.geometry_id,&SketchSegment::id);check(l!=s.segments.end(),"Contact line identity lost");
                const auto* a=s.find_point(l->first_point_id);const auto* b=s.find_point(l->second_point_id);
                check(std::abs((p->x-a->x)*(b->y-a->y)-(p->y-a->y)*(b->x-a->x))/std::hypot(b->x-a->x,b->y-a->y)<1e-6,"Line C failed");
            }
        }
        if(c.kind==ConstraintKind::PointOnCircle) {
            const auto a=std::ranges::find(s.arcs,c.geometry_id,&SketchArc::id);check(a!=s.arcs.end(),"Contact arc identity lost");
            const auto* center=s.find_point(a->center_point_id);
            check(std::abs(std::hypot(p->x-center->x,p->y-center->y)-a->radius)<1e-6,"Arc C failed");
        }
        if(c.kind==ConstraintKind::Tangent) {
            auto arc=std::ranges::find(s.arcs,c.geometry_id,&SketchArc::id);
            auto line=std::ranges::find(s.segments,c.second_geometry_id,&SketchSegment::id);
            if(arc==s.arcs.end()){arc=std::ranges::find(s.arcs,c.second_geometry_id,&SketchArc::id);line=std::ranges::find(s.segments,c.geometry_id,&SketchSegment::id);}
            check(arc!=s.arcs.end()&&line!=s.segments.end()&&p,"T identities lost");
            const auto* center=s.find_point(arc->center_point_id);const auto* a=s.find_point(line->first_point_id);const auto* b=s.find_point(line->second_point_id);
            check(std::abs((p->x-center->x)*(b->x-a->x)+(p->y-center->y)*(b->y-a->y))/std::hypot(b->x-a->x,b->y-a->y)<1e-6,"Independent T equation failed");
        }
    }
    for(const auto& d:s.dimensions)if(!d.suppressed) {
        if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter)
            check(std::abs(s.arcs.front().radius*(d.kind==DimensionKind::Diameter?2:1)-d.value)<1e-6,"R/D equation failed");
        else {
            const auto* p=s.find_point(d.first_point_id);const auto* q=s.find_point(d.second_point_id);
            const double dx=q->x-p->x,dy=q->y-p->y;
            const double actual=d.kind==DimensionKind::Distance?std::hypot(dx,dy):std::abs(d.kind==DimensionKind::DistanceX?dx:dy);
            check(std::abs(actual-std::abs(d.value))<1e-6,"Rectangle distance equation failed");
        }
    }
}
inline Sketch retrim(Sketch before,bool reverse) {
    before.dimensions.clear();before.dimension_layouts.clear();
    const auto arc=before.arcs.front();before.arcs.clear();before.circles.push_back({arc.id,arc.center_point_id,arc.radius});
    SketchConstraint incidence;incidence.id="upper-circle-contact";incidence.kind=ConstraintKind::PointOnCircle;
    incidence.first_point_id=arc.end_point_id;incidence.geometry_id=arc.id;before.constraints.push_back(incidence);
    const auto lower=*before.find_point(arc.start_point_id),upper=*before.find_point(arc.end_point_id),center=*before.find_point(arc.center_point_id);
    const auto right=before.add_segment(lower.x,lower.y,upper.x,upper.y);
    const auto contact=std::ranges::find_if(before.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;})->first_point_id;
    const auto cut=[&](std::array<double,2> p,const std::string& id) {
        const auto piece=nearest_sketch_trim_piece(sketch_trim_topology(before,false),p,1e-3);
        check(piece&&piece->geometry_id==id,"Trim recreation did not offer its expected piece");
        static_cast<void>(apply_sketch_trim(before,{*piece}));
    };
    if(reverse)cut({center.x,center.y},right);
    cut({center.x-arc.radius,center.y},arc.id);
    if(!reverse)cut({center.x,center.y},right);
    check(before.points.size()==5,"Trim retained two points at its native contact");
    check(before.arcs.front().start_point_id==contact||before.arcs.front().end_point_id==contact,"Trim lost persisted contact identity");
    check(std::ranges::none_of(before.constraints,[](const auto& c){return c.kind==ConstraintKind::PointOnCircle||(!c.geometry_id.starts_with("sketch_axis:")&&c.kind==ConstraintKind::PointOnLine);}),"Trim retained redundant endpoint C");
    equations(before);return before;
}
inline int run(const char* path) {
    std::ifstream input(path);check(bool(input),"Trimmed rectangle fixture missing");
    const auto source=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));
    unsigned cases=0;
    for(unsigned fixture=0;fixture<3;++fixture)for(bool rotated:{false,true})for(bool mirrored:{false,true})
    for(bool reverse:{false,true})for(bool diameter:{false,true})for(bool axial:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        if(!driving&&locked)continue; // Read-only measurements cannot lock values.
        ++cases;
        try {
            auto s=fixture?retrim(source,fixture==2):source;
            const auto bottom=source.dimensions[1];
            for(auto& p:s.points){if(mirrored)p.x=-p.x;if(rotated){const double x=p.x;p.x=-p.y;p.y=x;}}
            for(auto& c:s.constraints) {
                if(reverse&&c.kind==ConstraintKind::Tangent)std::swap(c.geometry_id,c.second_geometry_id);
                if(rotated){if(c.kind==ConstraintKind::Horizontal)c.kind=ConstraintKind::Vertical;else if(c.kind==ConstraintKind::Vertical)c.kind=ConstraintKind::Horizontal;
                    if(c.geometry_id=="sketch_axis:x")c.geometry_id="sketch_axis:y";else if(c.geometry_id=="sketch_axis:y")c.geometry_id="sketch_axis:x";}
            }
            for(auto& a:s.arcs) {
                if(mirrored)std::swap(a.start_point_id,a.end_point_id);
                const auto* c=s.find_point(a.center_point_id);const auto* p=s.find_point(a.start_point_id);const auto* q=s.find_point(a.end_point_id);
                a.start_angle=std::atan2(p->y-c->y,p->x-c->x);a.end_angle=std::atan2(q->y-c->y,q->x-c->x);
                while(a.end_angle<=a.start_angle)a.end_angle+=2*std::numbers::pi;
            }
            s.dimensions.clear();s.dimension_layouts.clear();
            auto radius=diameter?s.create_arc_diameter_dimension(s.arcs.front().id):s.create_arc_radius_dimension(s.arcs.front().id);
            std::string first=bottom.first_point_id,second=bottom.second_point_id;
            if(!s.find_point(first))first=std::ranges::find_if(s.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;})->first_point_id;
            if(reverse)std::swap(first,second);
            auto width=s.create_point_dimension(first,second,axial?(rotated?DimensionKind::DistanceY:DimensionKind::DistanceX):DimensionKind::Distance);
            radius.driving=width.driving=driving;radius.locked=width.locked=locked;
            s.apply_dimension(radius);s.apply_dimension(width);equations(s);
            const auto constraints=s.constraints;const auto count=s.points.size();
            for(const auto& id:{radius.id,width.id})for(double factor:{1.04,.97,1.02}) {
                const auto d=*std::ranges::find(s.dimensions,id,&SketchDimension::id);const auto before=s;
                check(s.set_dimension_value(id,d.value*factor),"Trimmed rectangle rejected feasible successive dimension edit");
                if(!driving)check(s.points==before.points&&s.arcs==before.arcs,"Reference dimension edited geometry");
                equations(s);s=Sketch::from_serialized(s.serialized());equations(s);
                check(s.constraints==constraints&&s.points.size()==count,"Dimension edit changed point/constraint identity");
            }
            const auto center=s.arcs.front().center_point_id;
            for(int edit=0;edit<3;++edit) {
                const auto p=*s.find_point(center);const auto before=s;
                const bool accepted=s.move_point(center,p.x+(rotated?0.:2.),p.y+(rotated?2.:0.));
                check(accepted==(!driving||!locked),"Trim center drag did not respect locked/unlocked DOF");
                if(accepted)check(std::hypot(s.find_point(center)->x-p.x,s.find_point(center)->y-p.y)>1e-3,"Trim center drag reported success without movement");
                else check(s.serialized()==before.serialized(),"Rejected locked drag changed Sketch");
                equations(s);s=Sketch::from_serialized(s.serialized());equations(s);
            }
            for(const auto& point:s.points) {
                auto moved=s;const auto before=moved.serialized();
                const bool origin=std::abs(point.x)<1e-6&&std::abs(point.y)<1e-6;
                const bool accepted=moved.move_point(point.id,point.x+2.,point.y-3.);
                const bool expected=!origin&&(!driving||!locked);
                if(accepted!=expected)std::cerr<<"Failed point "<<point.id<<" x="<<point.x<<" y="<<point.y<<std::endl;
                check(accepted==expected,"Trimmed rectangle native point did not respect its permitted DOF");
                if(accepted)check(std::hypot(moved.find_point(point.id)->x-point.x,moved.find_point(point.id)->y-point.y)>1e-3,"Point drag reported success without movement");
                else check(moved.serialized()==before,"Origin drag changed Sketch");
                equations(moved);
            }
            const auto before=s.serialized();check(!s.set_dimension_value(radius.id,-1.),"Negative radius accepted");check(s.serialized()==before,"Rejected radius changed Sketch");
            auto untied=s;const auto tangent=std::ranges::find_if(untied.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;});
            const auto contact=tangent->first_point_id;const auto tangent_id=tangent->id;
            const auto point_count=untied.points.size();untied.remove_constraint(tangent_id);
            check(untied.points.size()==point_count&&untied.find_point(contact),"Removing T deleted its native contact");
            if(fixture)check(untied.arcs.front().start_point_id==contact||untied.arcs.front().end_point_id==contact,"Removing T disconnected shared incidence");
        } catch(const std::exception& e) {
            std::cerr<<"Trim matrix fixture="<<fixture<<" rotated="<<rotated<<" mirrored="<<mirrored<<" reverse="<<reverse<<" diameter="<<diameter<<" axial="<<axial<<" driving="<<driving<<" locked="<<locked<<": "<<e.what()<<std::endl;throw;
        }
    }
    std::cout<<"Trim rectangle/circle matrix passed: "<<cases<<" variants; successive R/D and distance edits, drag, locks, reference dimensions, reopen and native identity"<<std::endl;
    return 0;
}
}
inline int run_trimmed_rectangle_arc_study(const char* path){return trimmed_rectangle_study::run(path);}
