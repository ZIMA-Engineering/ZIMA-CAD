#pragma once

#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <cmath>

inline void run_sketch_tangent_reference_studies() {
    using namespace zima::sketcher;
    const auto check=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    auto source=Sketch::create_default();
    auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    ref.source_document_id="study-part";ref.source_owner_id="fillet";
    ref.source_semantic_key="quarter-arc";
    ref.cached_points={{0,-7},{.228361402466,-8.148050297095},
        {.87867965644,-9.12132034356},{1.851949702905,-9.771638597534},{3,-10}};
    zima::kernel::BSplineGeometry exact;
    exact.degree=2;exact.poles={{0,-7,0},{0,-10,0},{3,-10,0}};
    exact.weights={1,std::sqrt(.5),1};exact.knots={0,0,0,1,1,1};ref.exact_spline=exact;
    source.add_external_reference(ref);
    if(const auto* path=std::getenv("ZIMA_TANGENT_TRIM_SOURCE")) {
        std::ifstream input(path);
        check(bool(input),"Actual FORM tangent study fixture is missing");
        source=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));
    }
    for(const bool transformed:{false,true})for(const bool profile_first:{false,true})for(const bool reverse:{false,true}) {
        auto s=source;const auto original=s.external_references.front();
        if(transformed) {
            s.resolved_origin={50,20,30};s.resolved_x_axis={-1,0,0};
            s.resolved_y_axis={0,0,-1};s.resolved_normal={0,-1,0};
        }
        std::string contour;
        if(profile_first)contour=s.add_external_profile_geometry(original.id);
        const auto circle=s.add_circle(-14,-10.6,13.63);
        static_cast<void>(s.add_tangent_constraint("sketch_axis:x",circle));
        static_cast<void>(s.add_tangent_constraint(reverse?circle:original.id,reverse?original.id:circle));
        if(!profile_first)contour=s.add_external_profile_geometry(original.id);
        const auto pieces=sketch_trim_topology(s,false);
        const auto count=std::ranges::count_if(pieces,[&](const auto& p){return p.geometry_id==contour;});
        check(count==2,"Tangent to an external fillet does not divide its Reference Profile; Trim deletes the complete contour");
        for(const auto& piece:pieces)if(piece.geometry_id==contour) {
            auto trimmed=s;static_cast<void>(apply_sketch_trim(trimmed,{piece}));
            check(!trimmed.bsplines.empty() && !trimmed.curve_trims.empty(),
                "Trimming a tangent Reference Profile deleted all its geometry");
            check(trimmed.external_references.front()==original,
                "Trimming a tangent Reference Profile changed its external source");
            check(Sketch::from_serialized(trimmed.serialized()).solve().status!=SolveStatus::Conflicting,
                "Tangent Reference Profile did not reopen and solve after trimming");
            auto radius=trimmed.create_circle_radius_dimension(circle);
            for(const double value:{15.,16.5,14.}) {
                radius.value=value;trimmed.apply_dimension(radius);
                check(std::ranges::none_of(trimmed.curve_trims,[](const auto& t){return t.broken;}),
                    "Radius edit breaks the Reference Profile trim at its tangent contact");
                const auto contact=std::ranges::find_if(trimmed.constraints,[&](const auto& c) {
                    return c.kind==ConstraintKind::Tangent && (c.geometry_id==original.id || c.second_geometry_id==original.id);
                });
                const auto* p=trimmed.find_point(contact->first_point_id);
                const auto shape=sketch_curve_geometry(trimmed,contour);
                const auto first=zima::kernel::bspline_value(shape,0),last=zima::kernel::bspline_value(shape,1);
                check(std::min(std::hypot(first.x-p->x,first.y-p->y),std::hypot(last.x-p->x,last.y-p->y))<1e-8,
                    "Trimmed Reference Profile endpoint does not follow its tangent point");
            }
            for(const auto& constraint:trimmed.constraints) {
                if(constraint.kind!=ConstraintKind::Tangent)continue;
                auto dragged=trimmed;
                const auto* p=dragged.find_point(constraint.first_point_id);
                const auto id=p->id;const double x=p->x,y=p->y;
                check(dragged.move_point(id,x-.15,y-.1),"Tangent contact cannot be dragged after Reference Profile trim");
                check(std::ranges::none_of(dragged.curve_trims,[](const auto& t){return t.broken;}),
                    "Dragging a tangent contact breaks its trimmed Reference Profile");
                check(dragged.external_references.front()==original,"Drag changed the external fillet source");
                auto reopened=Sketch::from_serialized(dragged.serialized());
                check(reopened.solve().status!=SolveStatus::Conflicting,"Trimmed tangent drag does not survive reopening");
            }
        }
    }
    // Known analytic solutions independently check the equation network rather
    // than merely accepting a successful solver status.
    for(const bool reverse:{false,true})for(const bool line_first:{false,true}) {
        auto s=source;const auto reference=s.external_references.front();
        const auto support=*external_reference_circle(reference);
        const double r=15.,y=-r,dy=y-support[1],distance=r+support[2];
        const double x=support[0]-std::sqrt(distance*distance-dy*dy);
        const double px=support[0]+support[2]*(x-support[0])/distance;
        const double py=support[1]+support[2]*dy/distance;
        const auto id=s.add_arc(x,y,x,0,px,py);
        const auto start=s.arcs.front().start_point_id,end=s.arcs.front().end_point_id;
        static_cast<void>(s.add_point_on_line_constraint(start,"sketch_axis:x"));
        static_cast<void>(s.add_point_on_circle_constraint(end,reference.id));
        const auto curved=[&]{static_cast<void>(s.add_tangent_constraint(reverse?id:reference.id,reverse?reference.id:id));};
        const auto straight=[&]{static_cast<void>(s.add_tangent_constraint("sketch_axis:x",id));};
        if(line_first){straight();curved();}else{curved();straight();}
        check(std::ranges::all_of(s.constraints,[](const auto& c){return c.kind!=ConstraintKind::Tangent || !c.first_point_id.empty();}),
            "Native arc tangency did not retain its existing endpoint contact");
        auto size=s.create_arc_radius_dimension(id);
        for(const double value:{16.,14.,15.5}) {
            size.value=value;s.apply_dimension(size);
            const auto* centre=s.find_point(s.arcs.front().center_point_id);
            check(std::abs(centre->y+value)<1e-8 &&
                std::abs(std::hypot(centre->x-support[0],centre->y-support[1])-value-support[2])<1e-8,
                "Native arc loses external/axis tangency after radius edit");
        }
        for(const auto& point_id:{s.arcs.front().center_point_id,start,end}) {
            auto dragged=s;const auto* p=dragged.find_point(point_id);
            check(dragged.move_point(point_id,p->x-.15,p->y-.1),"Native arc external tangent centre/endpoint cannot be dragged");
            check(dragged.external_references.front()==reference,"Native tangent arc drag changed its external source");
        }
        check(Sketch::from_serialized(s.serialized()).solve().status!=SolveStatus::Conflicting,
            "Native externally tangent arc fails after reopening");
    }
    for(const bool reverse:{false,true})for(const bool arcs:{false,true}) {
        auto s=Sketch::create_default();
        std::array<std::string,3> ids,centres;
        const std::array<std::array<double,2>,3> positions{{{-3,0},{3,0},{0,3*std::sqrt(3.)}}};
        for(unsigned i=0;i<3;++i) {
            const auto [x,y]=positions[i];const double angle=(-1.+2*i)*std::acos(-1.)/4;
            ids[i]=arcs?s.add_arc(x,y,x+3*std::cos(angle),y+3*std::sin(angle),x-3*std::cos(angle),y-3*std::sin(angle)):s.add_circle(x,y,3);
            centres[i]=arcs?s.arcs.back().center_point_id:s.circles.back().center_point_id;
        }
        s.find_point(centres[0])->fixed=true;
        for(unsigned i=0;i<3;++i)static_cast<void>(s.add_tangent_constraint(reverse?ids[(i+1)%3]:ids[i],reverse?ids[i]:ids[(i+1)%3]));
        auto size=arcs?s.create_arc_radius_dimension(ids[0]):s.create_circle_radius_dimension(ids[0]);s.apply_dimension(size);
        for(const double r:{4.,2.5,3.}) {
            size.value=r;
            try{s.apply_dimension(size);}catch(const std::exception& e){throw std::runtime_error(std::string(arcs?"Arc triangle edit: ":"Circle triangle edit: ")+e.what());}
            for(unsigned i=0;i<3;++i) {
                const auto* p=s.find_point(centres[i]);const auto* q=s.find_point(centres[(i+1)%3]);
                const double ra=arcs?s.arcs[i].radius:s.circles[i].radius;
                const double rb=arcs?s.arcs[(i+1)%3].radius:s.circles[(i+1)%3].radius;
                check(std::abs(std::hypot(p->x-q->x,p->y-q->y)-ra-rb)<1e-8,
                    "Three mutually tangent circles lose a tangency after a dimension edit");
            }
        }
        const auto* p=s.find_point(centres[2]);
        check(s.move_point(p->id,p->x+.2,p->y+.15),"Three mutually tangent circles cannot be dragged");
        check(Sketch::from_serialized(s.serialized()).solve().status!=SolveStatus::Conflicting,
            "Three mutually tangent circles fail after reopening");
    }
    for(const bool reverse:{false,true})for(const bool arcs:{false,true})for(const bool diameter:{false,true}) {
        auto s=Sketch::create_default();std::array<std::string,3> ids,centres;
        for(unsigned i=0;i<3;++i) {
            const double x=6.*i;
            ids[i]=arcs?s.add_arc(x,0,x+3,0,x-3,0):s.add_circle(x,0,3);
            centres[i]=arcs?s.arcs.back().center_point_id:s.circles.back().center_point_id;
        }
        s.find_point(centres[0])->fixed=true;
        for(unsigned i=1;i<3;++i) {
            SketchConstraint c;c.id=Sketch::create_point(0,0).id;c.kind=ConstraintKind::PointOnLine;
            c.first_point_id=centres[i];c.geometry_id="sketch_axis:x";s.constraints.push_back(c);
        }
        for(unsigned i=0;i<2;++i)static_cast<void>(s.add_tangent_constraint(reverse?ids[i+1]:ids[i],reverse?ids[i]:ids[i+1]));
        std::array<SketchDimension,3> sizes;
        for(unsigned i=0;i<3;++i) {
            sizes[i]=arcs?s.create_arc_radius_dimension(ids[i]):s.create_circle_radius_dimension(ids[i]);
            if(diameter){sizes[i].kind=DimensionKind::Diameter;sizes[i].value*=2;}
            try{s.apply_dimension(sizes[i]);}catch(const std::exception& e){throw std::runtime_error(std::string(arcs?"Arc chain initial size ":"Circle chain initial size ")+std::to_string(i)+": "+e.what());}
        }
        for(const double r:{4.,2.5,3.5}) {
            sizes[1].value=diameter?2*r:r;
            try{s.apply_dimension(sizes[1]);}catch(const std::exception& e){throw std::runtime_error(std::string(arcs?"Arc chain edit: ":"Circle chain edit: ")+e.what());}
            for(unsigned i=0;i<2;++i) {
                const auto* a=s.find_point(centres[i]);const auto* b=s.find_point(centres[i+1]);
                check(std::abs(std::hypot(a->x-b->x,a->y-b->y)-(3+r))<1e-8,
                    "Three circular curves lose tangency after a radius edit");
            }
        }
        const auto* p=s.find_point(centres[1]);
        check(s.move_point(p->id,p->x+.25,p->y),arcs?"Middle arc cannot be dragged with unlocked radius dimensions":"Middle circle cannot be dragged with unlocked radius dimensions");
        for(unsigned i=0;i<2;++i) {
            const auto* a=s.find_point(centres[i]);const auto* b=s.find_point(centres[i+1]);
            const double ra=arcs?s.arcs[i].radius:s.circles[i].radius;
            const double rb=arcs?s.arcs[i+1].radius:s.circles[i+1].radius;
            check(std::abs(std::hypot(a->x-b->x,a->y-b->y)-(ra+rb))<1e-8,
                "Three circular curves lose tangency while dragging");
        }
        check(Sketch::from_serialized(s.serialized()).solve().status!=SolveStatus::Conflicting,
            "Three circular curves do not reopen with their tangencies");
        for(auto& d:s.dimensions)d.locked=true;
        const auto unchanged=s.serialized();p=s.find_point(centres[1]);
        check(!s.move_point(p->id,p->x+.2,p->y) && s.serialized()==unchanged,
            "Locked tangent-chain radius drivers did not reject dragging transactionally");
    }
    for(const bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto outer=s.add_circle(0,0,10),inner=s.add_circle(7,0,3);
        s.find_point(s.circles.front().center_point_id)->fixed=true;
        static_cast<void>(s.add_point_on_line_constraint(s.circles.back().center_point_id,"sketch_axis:x"));
        static_cast<void>(s.add_tangent_constraint(reverse?inner:outer,reverse?outer:inner));
        check(s.constraints.back().tangent_internal,"Internal circular tangent side was lost");
        auto fixed=s.create_circle_radius_dimension(outer);fixed.locked=true;s.apply_dimension(fixed);
        auto radius=s.create_circle_radius_dimension(inner);s.apply_dimension(radius);
        for(const double r:{4.,2.5,3.5}) {
            radius.value=r;s.apply_dimension(radius);const auto* p=s.find_point(s.circles.back().center_point_id);
            check(std::abs(std::hypot(p->x,p->y)-(10-r))<1e-8,"Internal circular tangency was changed by radius editing");
        }
        const auto* p=s.find_point(s.circles.back().center_point_id);
        check(s.move_point(p->id,p->x+.2,p->y),"Internal tangent circle cannot be dragged with unlocked radius");
        p=s.find_point(s.circles.back().center_point_id);
        check(std::abs(std::hypot(p->x,p->y)-(10-s.circles.back().radius))<1e-8,
            "Internal tangent circle drag changed its contact side");
    }
    for(const bool vertical:{false,true})for(const double sign:{-1.,1.}) {
        auto s=Sketch::create_default();
        const auto ids=vertical?s.add_rectangle(-4*sign,-8,4*sign,13):s.add_rectangle(-8,-4*sign,13,4*sign);
        const auto edge=s.segments[vertical?0:1];
        static_cast<void>(s.add_symmetric_constraint(edge.first_point_id,edge.second_point_id,vertical?"sketch_axis:y":"sketch_axis:x"));
        auto width=s.create_segment_dimension(ids[0]),height=s.create_segment_dimension(ids[1]);
        s.apply_dimension(width);s.apply_dimension(height);
        for(const double value:{18.,22.,16.}) {width.value=value;s.apply_dimension(width);}
        const auto verify=[&](const Sketch& value) {
            for(const auto index:{vertical?0u:1u,vertical?2u:3u}) {
                const auto& e=value.segments[index];const auto* a=value.find_point(e.first_point_id);const auto* b=value.find_point(e.second_point_id);
                check(std::abs(vertical?a->x+b->x:a->y+b->y)<1e-8 &&
                    std::abs(vertical?a->y-b->y:a->x-b->x)<1e-8,"Rectangle corners lose axis symmetry");
            }
        };
        verify(s);
        const auto corner=s.segments[0].first_point_id;const auto* p=s.find_point(corner);
        check(s.move_point(corner,p->x+.3,p->y+.2),"Symmetric rectangle corner cannot be dragged");verify(s);
        auto reopened=Sketch::from_serialized(s.serialized());
        check(reopened.solve().status!=SolveStatus::Conflicting,"Symmetric rectangle does not reopen");verify(reopened);
    }
}
