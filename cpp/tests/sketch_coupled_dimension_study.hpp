#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace zima::test {
inline void check_coupled_equations(const sketcher::Sketch& s) {
    using namespace sketcher;
    const auto near=[](double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-6)
        throw std::runtime_error("Independent coupled Sketch equation failed: "+std::to_string(a)+" versus "+std::to_string(b));};
    const auto point=[&](const std::string& id)->std::array<double,2> {
        if(id=="sketch_origin")return {0,0};
        if(const auto* p=s.find_point(id))return {p->x,p->y};
        const auto ref=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
        if(ref!=s.external_references.end()&&ref->cached_points.size()==1)return ref->cached_points.front();
        throw std::runtime_error("Independent point lookup failed");
    };
    const auto line=[&](const std::string& id)->std::array<double,4> {
        if(id=="sketch_axis:x")return {0,0,1,0};if(id=="sketch_axis:y")return {0,0,0,1};
        const auto found=std::ranges::find(s.segments,id,&SketchSegment::id);
        if(found==s.segments.end())throw std::runtime_error("Independent line lookup failed");
        const auto a=point(found->first_point_id),b=point(found->second_point_id);
        const double length=std::hypot(b[0]-a[0],b[1]-a[1]);
        return {a[0],a[1],(b[0]-a[0])/length,(b[1]-a[1])/length};
    };
    for(const auto& a:s.arcs) {
        const auto c=point(a.center_point_id);
        for(const auto& id:{a.start_point_id,a.end_point_id}) {
            const auto p=point(id);near(std::hypot(p[0]-c[0],p[1]-c[1]),a.radius);
        }
    }
    for(const auto& c:s.constraints)if(!c.suppressed) {
        if(c.kind==ConstraintKind::EqualRadius)near(*s.circular_radius(c.geometry_id),*s.circular_radius(c.second_geometry_id));
        else if(c.kind==ConstraintKind::PointReference||c.kind==ConstraintKind::Coincident) {
            const auto a=point(c.first_point_id),b=point(c.second_point_id);near(a[0],b[0]);near(a[1],b[1]);
        } else if(c.kind==ConstraintKind::PointOnLine) {
            const auto a=point(c.first_point_id);const auto l=line(c.geometry_id);near((a[0]-l[0])*l[3]-(a[1]-l[1])*l[2],0);
        } else if(c.kind==ConstraintKind::Symmetric) {
            const auto a=point(c.first_point_id),b=point(c.second_point_id);const auto l=line(c.geometry_id);
            const double along=(a[0]-l[0])*l[2]+(a[1]-l[1])*l[3];
            near(a[0]+b[0],2*(l[0]+along*l[2]));near(a[1]+b[1],2*(l[1]+along*l[3]));
        } else if(c.kind==ConstraintKind::Tangent) {
            const bool first=s.circular_radius(c.geometry_id).has_value();
            const auto arc=std::ranges::find(s.arcs,first?c.geometry_id:c.second_geometry_id,&SketchArc::id);
            const auto p=point(c.first_point_id),center=point(arc->center_point_id);const auto l=line(first?c.second_geometry_id:c.geometry_id);
            near(((p[0]-center[0])*l[2]+(p[1]-center[1])*l[3])/arc->radius,0);
        } else throw std::runtime_error("Independent constraint equation is missing");
    }
    for(const auto& d:s.dimensions)if(!d.suppressed) {
        double actual{};
        if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter)
            actual=*s.circular_radius(d.geometry_id)*(d.kind==DimensionKind::Diameter?2:1);
        else if(d.kind==DimensionKind::DistanceLine) {
            const auto a=line(d.geometry_id),b=line(d.second_geometry_id);
            actual=(a[2]*(b[1]-a[1])-a[3]*(b[0]-a[0]))*d.solution_side;
        } else if(d.kind==DimensionKind::DistancePointLine) {
            const auto p=point(d.first_point_id);const auto l=line(d.geometry_id);
            actual=((p[1]-l[1])*l[2]-(p[0]-l[0])*l[3])*d.solution_side;
        } else if(d.kind==DimensionKind::Distance||d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY||d.kind==DimensionKind::Angle) {
            const auto a=point(d.first_point_id),b=point(d.second_point_id);
            actual=d.kind==DimensionKind::Distance?std::hypot(b[0]-a[0],b[1]-a[1]):
                d.kind==DimensionKind::DistanceX?b[0]-a[0]:d.kind==DimensionKind::DistanceY?b[1]-a[1]:
                std::atan2(b[1]-a[1],b[0]-a[0])*180/std::numbers::pi;
            if(d.kind==DimensionKind::Angle)actual=d.value+std::remainder(actual-d.value,360.);
        } else throw std::runtime_error("Independent dimension equation is missing");
        near(actual,d.value);
    }
}

inline sketcher::Sketch many_arc_profile(double rotation) {
    using namespace sketcher;
    auto s=Sketch::create_default();const double pi=std::numbers::pi;
    for(unsigned i=0;i<12;++i) {
        const double a=rotation+i*pi/6,x=50*std::cos(a),y=50*std::sin(a);
        static_cast<void>(s.add_arc(x,y,x+2*std::cos(a-pi/12),y+2*std::sin(a-pi/12),
            x+2*std::cos(a+pi/12),y+2*std::sin(a+pi/12)));
    }
    for(unsigned i=0;i<12;++i) {
        const auto *a=s.find_point(s.arcs[i].end_point_id),*b=s.find_point(s.arcs[(i+1)%12].start_point_id);
        static_cast<void>(s.add_segment(a->x,a->y,b->x,b->y));
    }
    static_cast<void>(s.add_circle(0,0,3));static_cast<void>(s.add_circle(5,0,1));
    s.dimensions.push_back(s.create_line_pair_dimension(s.segments[0].id,s.segments[6].id,DimensionKind::DistanceLine));
    s.dimensions.push_back(s.create_line_pair_dimension(s.segments[2].id,s.segments[8].id,DimensionKind::DistanceLine));
    return s;
}
inline sketcher::Sketch coupled_radius_profile(bool mixed_pair, bool reverse) {
    using namespace sketcher;
    auto s=Sketch::create_default();
    const double angle=std::asin(.4),pi=std::numbers::pi;
    const auto arc=[&](double y,double radius,double first,double last) {
        return s.add_arc(0,y,radius*std::cos(first),y+radius*std::sin(first),
            radius*std::cos(last),y+radius*std::sin(last));
    };
    const auto top=arc(250,50,angle,pi-angle);
    const auto right=arc(0,150,-angle,angle);
    const auto left=arc(0,150,pi-angle,pi+angle);
    const auto bottom=arc(-250,50,pi+angle,2*pi-angle);
    const auto connect=[&](const std::string& a,bool a_start,const std::string& b,bool b_start) {
        const auto first=std::ranges::find(s.arcs,a,&SketchArc::id);
        const auto second=std::ranges::find(s.arcs,b,&SketchArc::id);
        const auto *p=s.find_point(a_start?first->start_point_id:first->end_point_id),
            *q=s.find_point(b_start?second->start_point_id:second->end_point_id);
        const auto p_id=p->id,q_id=q->id;
        const auto line=s.add_segment(p->x,p->y,q->x,q->y);
        for(const auto& [curve,point]:{std::pair{a,p_id},std::pair{b,q_id}}) {
            SketchConstraint c;c.id="contact-"+std::to_string(s.constraints.size());
            c.kind=ConstraintKind::Tangent;c.first_point_id=point;
            c.geometry_id=reverse?line:curve;c.second_geometry_id=reverse?curve:line;
            s.constraints.push_back(std::move(c));
        }
    };
    connect(right,false,top,true);connect(left,true,top,false);
    connect(right,true,bottom,false);connect(left,false,bottom,true);
    const auto relation=[&](ConstraintKind kind,const std::string& first,
            const std::string& second,const std::string& geometry={}) {
        SketchConstraint c;c.id="relation-"+std::to_string(s.constraints.size());c.kind=kind;
        if(kind==ConstraintKind::EqualRadius) {c.geometry_id=first;c.second_geometry_id=second;}
        else {c.first_point_id=first;c.second_point_id=second;c.geometry_id=geometry;}
        s.constraints.push_back(std::move(c));
    };
    const auto center=s.arcs[1].center_point_id;
    relation(ConstraintKind::PointReference,center,"sketch_origin");
    relation(ConstraintKind::PointOnLine,s.arcs[0].center_point_id,{},"sketch_axis:y");
    relation(ConstraintKind::Symmetric,s.arcs[0].center_point_id,s.arcs[3].center_point_id,"sketch_axis:x");
    for(const auto& [a,b]:{std::pair{s.arcs[0].start_point_id,s.arcs[3].end_point_id},
            std::pair{s.arcs[0].end_point_id,s.arcs[3].start_point_id},
            std::pair{s.arcs[1].start_point_id,s.arcs[1].end_point_id},
            std::pair{s.arcs[2].start_point_id,s.arcs[2].end_point_id}})
        relation(ConstraintKind::Symmetric,a,b,"sketch_axis:x");
    relation(ConstraintKind::EqualRadius,top,bottom);
    relation(ConstraintKind::EqualRadius,right,left);
    if(mixed_pair) {
        const auto circle=s.add_circle(0,0,50);
        relation(ConstraintKind::EqualRadius,reverse?circle:bottom,reverse?bottom:circle);
    }
    auto span=s.create_point_dimension(s.arcs[0].center_point_id,s.arcs[3].center_point_id,DimensionKind::DistanceY);
    s.dimensions.push_back(span);
    s.dimensions.push_back(s.create_arc_radius_dimension(top));
    s.dimensions.push_back(s.create_arc_radius_dimension(right));
    s.validate();return s;
}

inline int run_sketch_coupled_dimension_study() {
    using namespace sketcher;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    unsigned radial_cases{},linear_cases{};
    for(bool reverse:{false,true})for(bool horizontal:{false,true})for(bool diameter:{false,true})
    for(bool external_anchor:{false,true})for(int state=0;state<3;++state) {
        auto s=coupled_radius_profile(true,reverse);
        if(horizontal) {
            for(auto& p:s.points){const double x=p.x;p.x=-p.y;p.y=x;}
            for(auto& a:s.arcs){a.start_angle+=std::numbers::pi/2;a.end_angle+=std::numbers::pi/2;}
            for(auto& c:s.constraints)if(c.geometry_id=="sketch_axis:x")c.geometry_id="sketch_axis:y";
                else if(c.geometry_id=="sketch_axis:y")c.geometry_id="sketch_axis:x";
            s.dimensions.front().kind=DimensionKind::DistanceX;s.dimensions.front().value=500;
        }
        if(external_anchor) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
            ref.source_document_id="immutable-source";ref.source_owner_id="source-feature";ref.source_semantic_key="source-point";ref.cached_points={{0,0}};
            s.external_references.push_back(ref);
            for(auto& c:s.constraints)if(c.kind==ConstraintKind::PointReference)c.second_point_id=ref.id;
        }
        const auto sources=s.external_references;const auto contacts=s.constraints;
        s.dimensions[0].locked=true;s.dimensions[1].locked=true;
        const auto driven=s.arcs[reverse?2:1].id;
        s.dimensions.pop_back();
        auto size=diameter?s.create_arc_diameter_dimension(driven):s.create_arc_radius_dimension(driven);
        size.driving=state!=0;size.locked=state==2;
        if(size.driving)size.value=diameter?310:155;
        try{s.apply_dimension(size);}catch(const std::exception& e) {
            throw std::runtime_error("Coupled radius insertion state="+std::to_string(state)+": "+e.what());
        }
        check_coupled_equations(s);
        if(size.driving)for(double value:{145.,160.,150.}) {
            check(s.set_dimension_value(size.id,diameter?2*value:value),"Coupled radius edit failed");check_coupled_equations(s);
            check(std::abs(s.arcs[1].radius-value)<1e-7&&std::abs(s.arcs[2].radius-value)<1e-7,"Equal-radius edit missed a companion arc");
            s=Sketch::from_serialized(s.serialized());check_coupled_equations(s);
        }
        for(double value:{152.,148.,154.}) {
            const auto before=s.serialized();const auto* p=s.find_point(s.arcs[1].end_point_id);
            const std::array old{p->x,p->y};const double angle=std::asin((value-50)/250),x=value*std::cos(angle),y=value*std::sin(angle);
            const bool moved=s.move_point(p->id,horizontal?-y:x,horizontal?x:y);
            if(state==2)check(!moved&&s.serialized()==before,"Locked fully constrained radius escaped on drag");
            else {
                check(moved,"Editable coupled arc rejected its endpoint drag");check_coupled_equations(s);
                const auto* changed=s.find_point(s.arcs[1].end_point_id);check(std::hypot(changed->x-old[0],changed->y-old[1])>1e-4,"Coupled arc drag reported no movement");
            }
        }
        check(s.constraints==contacts&&s.external_references==sources,"Coupled edit changed source identity or read-only geometry");
        auto fixed=s;for(auto& p:fixed.points)p.fixed=true;const auto frozen=fixed.serialized();
        check(!fixed.set_dimension_value(size.id,diameter?340:170)||!size.driving,"Fixed coupled radius accepted an impossible dimension");
        if(size.driving)check(fixed.serialized()==frozen,"Rejected fixed radius edit changed the Sketch");
        auto part=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();s.owner_container_id=owner.id;
        part.history={owner};part.sketches={s};document::DocumentSession session(part);auto changed=part;
        changed.sketches.front().remove_dimension(size.id);session.commit_metadata(changed);
        check(session.undo()&&session.document().sketches.front().serialized()==s.serialized()&&session.redo()&&
            session.document().sketches.front().dimensions.size()+1==s.dimensions.size(),"Coupled dimension Undo/Redo failed");
        check(document::PartDocument::from_serialized(part.serialized()).sketches.front().serialized()==s.serialized(),"Coupled dimension native document roundtrip failed");
        ++radial_cases;
    }
    for(double orientation:{0.,.7})for(bool reverse:{false,true})for(int state=0;state<3;++state)
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,
            DimensionKind::DistanceLine,DimensionKind::DistancePointLine,DimensionKind::Angle,DimensionKind::Radius,DimensionKind::Diameter}) {
        auto s=many_arc_profile(orientation);const auto constraints=s.constraints;
        const auto& line=s.segments[3];
        auto d=kind==DimensionKind::DistanceLine?s.create_line_pair_dimension(reverse?s.segments[9].id:line.id,reverse?line.id:s.segments[9].id,kind):
            kind==DimensionKind::DistancePointLine?s.create_point_line_dimension(line.first_point_id,"sketch_axis:y"):
            kind==DimensionKind::Radius?s.create_arc_radius_dimension(s.arcs[3].id):
            kind==DimensionKind::Diameter?s.create_arc_diameter_dimension(s.arcs[3].id):
            kind==DimensionKind::Angle?s.create_segment_dimension(line.id,kind):
            s.create_point_dimension(reverse?line.second_point_id:line.first_point_id,reverse?line.first_point_id:line.second_point_id,kind);
        d.driving=state!=0;d.locked=state==2;s.apply_dimension(d);check_coupled_equations(s);
        if(d.driving)for(double delta:{.4,-.3,.2}) {
            auto updated=*std::ranges::find(s.dimensions,d.id,&SketchDimension::id);updated.value+=delta;
            s.apply_dimension(updated);check_coupled_equations(s);
            s=Sketch::from_serialized(s.serialized());check_coupled_equations(s);
        }
        for(double delta:{.15,-.2,.1}) {
            const auto* p=s.find_point(s.arcs[10].center_point_id);const auto old=*p;
            check(s.move_point(p->id,p->x+delta,p->y+delta*.7),"Many-arc free center drag failed");check_coupled_equations(s);
            p=s.find_point(old.id);check(std::hypot(p->x-old.x,p->y-old.y)>1e-4,"Many-arc drag reported no movement");
        }
        check(s.constraints==constraints,"Many-arc dimension changed incidence contracts");++linear_cases;
    }
    if(const auto* file=std::getenv("ZIMA_CPP_VERIFY_COUPLED_DIMENSION_DOCUMENT")) {
        const auto part=document::PartDocument::load(std::filesystem::u8path(file));
        for(const auto& source:part.sketches)for(const auto& initial:source.dimensions)if(initial.driving&&!initial.suppressed) {
            auto s=source;
            std::cout<<"Local dimension "<<source.id<<"/"<<initial.id<<" value="<<initial.value<<std::endl;
            for(const double value:{initial.value+5,initial.value-5,initial.value}) {
                auto d=*std::ranges::find(s.dimensions,initial.id,&SketchDimension::id);
                set_dimension_input_value(d,value,s.plane);
                const auto start=std::chrono::steady_clock::now();
                try {s.apply_dimension(d);}
                catch(const std::exception& e){throw std::runtime_error("Local dimension "+initial.id+": "+e.what());}
                std::cout<<"  "<<value<<" in "<<std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count()<<" ms\n";
                if(source.points.size()>32||std::ranges::any_of(source.dimensions,[](const auto& d){
                    return d.kind==DimensionKind::Radius&&std::abs(d.value-150)<1e-6;}))check_coupled_equations(s);
            }
        }
    }
    std::cout<<"Coupled dimension edit study: "<<radial_cases<<" radial and "<<linear_cases<<" many-arc cases passed\n";return 0;
}
}
