#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

inline void run_form_sweep_profile_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    std::ifstream file(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/form-sweep-profile.json");
    const auto original=Sketch::from_serialized(std::string((std::istreambuf_iterator<char>(file)),{}));
    const auto radius_id=original.dimensions[0].id,angle_id=original.dimensions[1].id;
    const auto center_id=original.arcs[0].center_point_id,contact_id=original.arcs[0].start_point_id;
    const auto end_id=original.arcs[0].end_point_id,bottom_id=original.points[1].id,leg_id=original.points[2].id;
    const auto line_contact_id=original.points[3].id;
    // Keep the exact user-authored base-axis variant in addition to the
    // transformed matrix below (whose axes are finite fixed native supports).
    auto exact=original;
    for(double r:{2.,4.,3.})check(exact.set_dimension_value(radius_id,r),"Actual FORM radius edit failed");
    for(double angle:{30.,60.,43.98734957728761})check(exact.set_dimension_value(angle_id,angle),"Actual FORM angular edit failed");
    for(const auto& id:{end_id,line_contact_id,bottom_id}) {
        const auto before=exact.serialized();const auto* p=exact.find_point(id);const double x=p->x,y=p->y;
        check(!exact.move_point(id,x+1,y+1)&&exact.serialized()==before,"Actual FORM immovable point drag changed document");
    }
    std::size_t cases=0,actions=0;
    for(bool external:{false,true})for(bool mirror:{false,true})for(bool reverse:{false,true})
    for(bool diameter:{false,true})for(double rotation:{0.,.37})for(int locks=0;locks<4;++locks) {
        auto s=original;
        // Represent axes as fixed native lines so the complete fixture can be
        // rotated/reflected without changing its incidence or source IDs.
        const auto add_line=[&](const std::string& id,std::array<double,2> a,std::array<double,2> b){
            auto first=Sketch::create_point(a[0],a[1]),second=Sketch::create_point(b[0],b[1]);
            first.fixed=second.fixed=true;
            const auto first_id=first.id,second_id=second.id;s.points.push_back(first);s.points.push_back(second);
            auto line=Sketch::create_segment(first_id,second_id);line.id=id;s.segments.push_back(line);
        };
        add_line("fixture-axis-x",{-100,0},{100,0});add_line("fixture-axis-y",{0,-100},{0,100});
        for(auto& c:s.constraints){if(c.geometry_id=="sketch_axis:x")c.geometry_id="fixture-axis-x";
            if(c.geometry_id=="sketch_axis:y")c.geometry_id="fixture-axis-y";}
        s.dimensions[1].geometry_id="fixture-axis-x";
        if(!external){const auto ref=s.external_references.front();add_line(ref.id,{100,-10},{-100,-10});s.external_references.clear();}
        const auto world=[&](double x,double y){if(mirror)x=-x;return std::array{x*std::cos(rotation)-y*std::sin(rotation),x*std::sin(rotation)+y*std::cos(rotation)};};
        const auto local=[&](const SketchPoint* p){const double x=p->x*std::cos(rotation)+p->y*std::sin(rotation);
            return std::array{mirror?-x:x,-p->x*std::sin(rotation)+p->y*std::cos(rotation)};};
        for(auto& p:s.points){const auto q=world(p.x,p.y);p.x=q[0];p.y=q[1];}
        for(auto& ref:s.external_references)for(auto& p:ref.cached_points)p=world(p[0],p[1]);
        for(auto& arc:s.arcs){if(mirror)std::swap(arc.start_point_id,arc.end_point_id);
            const auto* c=s.find_point(arc.center_point_id);const auto* a=s.find_point(arc.start_point_id);const auto* b=s.find_point(arc.end_point_id);
            arc.start_angle=std::atan2(a->y-c->y,a->x-c->x);arc.end_angle=std::atan2(b->y-c->y,b->x-c->x);
            while(arc.end_angle<=arc.start_angle)arc.end_angle+=2*std::acos(-1.);}
        if(reverse)std::swap(s.dimensions[1].geometry_id,s.dimensions[1].second_geometry_id);
        if(diameter){s.dimensions[0].kind=DimensionKind::Diameter;s.dimensions[0].value*=2;}
        s.dimensions[0].locked=(locks&1)!=0;s.dimensions[1].locked=(locks&2)!=0;
        const auto sources=s.external_references;const auto relations=s.constraints;
        const auto verify=[&](const Sketch& sketch){
            const auto& arc=sketch.arcs[0];const auto c=local(sketch.find_point(center_id)),a=local(sketch.find_point(contact_id));
            const auto e=local(sketch.find_point(end_id)),t=local(sketch.find_point(line_contact_id));
            const auto b=local(sketch.find_point(bottom_id)),q=local(sketch.find_point(leg_id));
            check(std::abs(c[0])<1e-7&&std::hypot(e[0],e[1])<1e-7&&std::hypot(t[0],t[1])<1e-7,"FORM tangent axis anchors escaped");
            check(std::abs(b[0])<1e-7&&std::abs(b[1]+10)<1e-7&&std::abs(q[1]+10)<1e-7,"FORM support anchors escaped");
            check(std::abs(std::hypot(a[0]-c[0],a[1]-c[1])-arc.radius)<1e-7&&std::abs(c[1]+arc.radius)<1e-7,"FORM endpoint-circle incidence or tangent side changed");
            const double dx=a[0]-q[0],dy=a[1]-q[1],length=std::hypot(dx,dy);
            check(length>1e-7&&std::abs(((a[0]-c[0])*dx+(a[1]-c[1])*dy)/length)<1e-7,"FORM sloped line is not tangent");
            for(const auto& d:sketch.dimensions)if(d.driving&&!d.suppressed){
                if(d.id==radius_id)check(std::abs(arc.radius-d.value*(diameter?.5:1.))<1e-7,"FORM driving radial equation failed");
                if(d.id==angle_id){check(std::abs(std::acos(std::clamp(-dx/length,-1.,1.))*180/std::acos(-1.)-d.value)<1e-6,"FORM driving angular equation failed");}
            }
            check(sketch.external_references==sources&&sketch.constraints==relations,"FORM source references or constraints changed");
            check(arc.end_angle>arc.start_angle&&arc.end_angle-arc.start_angle<std::acos(-1.),"FORM arc changed contact branch");
            sketch.validate();
        };
        const auto initial=s.solve();check(initial.status!=SolveStatus::Conflicting&&initial.status!=SolveStatus::Invalid,"Transformed FORM fixture did not solve");verify(s);
        for(double r:{2.,4.,5.,3.}){check(s.set_dimension_value(radius_id,r*(diameter?2:1)),"FORM radius/diameter edit rejected");verify(s);++actions;}
        for(double angle:{30.,50.,60.,43.98734957728761}){check(s.set_dimension_value(angle_id,angle),"FORM angle edit rejected");verify(s);++actions;}
        const auto drag=[&](const std::string& id,double x,double y,bool movable){
            const auto before=s.serialized();const auto old=*s.find_point(id);const auto target=world(x,y);
            const bool accepted=s.move_point(id,target[0],target[1]);
            if(movable)check(accepted&&std::hypot(s.find_point(id)->x-old.x,s.find_point(id)->y-old.y)>.1,"FORM feasible drag did not move");
            else {check(!accepted&&s.serialized()==before,"FORM constrained drag escaped or changed the document");}
            verify(s);++actions;
        };
        drag(end_id,1,1,false);drag(line_contact_id,1,1,false);drag(bottom_id,1,-9,false);
        if(!(locks&1))for(double y:{-2.,-4.,-3.})drag(center_id,0,y,true);
        else drag(center_id,0,-2,false);
        if(!(locks&2))for(double theta:{.9,1.1,.8030722474454918}){
            const auto c=local(s.find_point(center_id));const double r=s.arcs[0].radius;
            drag(contact_id,c[0]+r*std::cos(theta),c[1]+r*std::sin(theta),true);
        } else if(!(locks&1)){
            const auto c=local(s.find_point(center_id));const auto a=local(s.find_point(contact_id));
            drag(contact_id,a[0]*1.25,a[1]*1.25,true);
        } else {const auto a=local(s.find_point(contact_id));drag(contact_id,a[0]+1,a[1]+1,false);}
        const auto before=s.serialized();check(!s.set_dimension_value(radius_id,-1)&&s.serialized()==before,"FORM invalid radius was not atomic");
        s=Sketch::from_serialized(s.serialized());verify(s);

        const bool radius_ok=s.set_dimension_value(radius_id,3*(diameter?2:1)),angle_ok=s.set_dimension_value(angle_id,43.98734957728761);

        check(radius_ok&&angle_ok,"FORM edits failed after reopening");verify(s);
        for(const auto& id:{radius_id,angle_id}){
            auto measured=s;auto d=*std::ranges::find(measured.dimensions,id,&SketchDimension::id);d.driving=false;d.locked=false;measured.apply_dimension(d);
            const auto points=measured.points;const auto arcs=measured.arcs;check(measured.set_dimension_value(id,id==angle_id?80:999),"FORM reference dimension edit rejected");
            check(measured.points==points&&measured.arcs==arcs,"FORM reference dimension drove geometry");verify(measured);
        }
        ++cases;
    }
    std::cout<<"FORM sweep: "<<cases<<" transformed native/external R/D and angle variants, "<<actions<<" successive edits/drags, locked and reference dimensions, independent equations and serialized reopen passed\n";
}

inline void run_external_point_circle_segment_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    std::size_t cases=0;
    for(bool reverse:{false,true})for(double side:{-1.,1.})for(double rotation:{0.,.61})
    for(bool diameter:{false,true})for(bool fixed_center:{false,true})for(bool locked:{false,true}) {
        auto s=Sketch::create_default();
        const auto world=[&](double x,double y){return std::array{x*std::cos(rotation)-y*std::sin(rotation),x*std::sin(rotation)+y*std::cos(rotation)};};
        const auto center=world(100,-40),start=world(40,-40);const double theta=side*std::acos(-1./3.);
        const auto contact=world(100+20*std::cos(theta),-40+20*std::sin(theta));
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
        ref.source_document_id="circle-point-source";ref.source_owner_id="source-point";ref.source_semantic_key="point";ref.cached_points={start};
        s.add_external_reference(ref);const auto circle=s.add_circle(center[0],center[1],20);
        const auto segment=s.add_segment(start[0],start[1],contact[0],contact[1]);
        const auto first=s.segments.back().first_point_id,second=s.segments.back().second_point_id;
        check(std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointReference&&c.first_point_id==first&&c.second_point_id==ref.id;}),"External segment start not bound");
        static_cast<void>(s.add_point_on_circle_constraint(second,circle));
        static_cast<void>(s.add_tangent_constraint(reverse?segment:circle,reverse?circle:segment,second));
        s.find_point(s.circles.front().center_point_id)->fixed=fixed_center;
        auto size=s.create_circle_radius_dimension(circle);size.kind=diameter?DimensionKind::Diameter:DimensionKind::Radius;size.value=diameter?40:20;size.locked=locked;s.apply_dimension(size);
        const auto verify=[&](const Sketch& value){
            const auto* p=value.find_point(first);const auto* q=value.find_point(second);const auto* c=value.find_point(value.circles.front().center_point_id);const auto r=value.circles.front().radius;
            check(std::hypot(p->x-start[0],p->y-start[1])<1e-7&&value.external_references.front()==ref,"External start moved or source changed");
            check(std::abs(std::hypot(q->x-c->x,q->y-c->y)-r)<1e-7&&std::abs(((q->x-p->x)*(q->x-c->x)+(q->y-p->y)*(q->y-c->y))/std::hypot(q->x-p->x,q->y-p->y))<1e-7,"External segment C+T equations failed");
            if(fixed_center)check(std::hypot(c->x-center[0],c->y-center[1])<1e-7,"Circle fixed center moved");
        };
        verify(s);
        for(double r:{18.,22.,20.}){check(s.set_dimension_value(size.id,r*(diameter?2:1)),"External point circle radius/diameter edit failed");verify(s);}
        const auto before=s.serialized();check(!s.move_point(first,start[0]+1,start[1]+1)&&s.serialized()==before,"External start drag not atomic");
        if(fixed_center&&locked){const auto* p=s.find_point(second);check(!s.move_point(second,p->x+1,p->y+1)&&s.serialized()==before,"Fully constrained tangent segment escaped");}
        else {
            const auto* p=s.find_point(second);const double x=p->x,y=p->y;
            const auto desired=fixed_center?world(100+22*std::cos(side*std::acos(-22./60.)),-40+22*std::sin(side*std::acos(-22./60.))):std::array{x+.25,y+.15};
            const bool accepted=s.move_point(second,desired[0],desired[1]);
            if(!accepted)throw std::runtime_error("External point tangent second end drag failed: reverse="+std::to_string(reverse)+", side="+std::to_string(side)+", rotation="+std::to_string(rotation)+", diameter="+std::to_string(diameter)+", fixed_center="+std::to_string(fixed_center)+", locked="+std::to_string(locked));
            check(std::hypot(s.find_point(second)->x-x,s.find_point(second)->y-y)>.05,"External point tangent second end did not move");verify(s);
        }
        s=Sketch::from_serialized(s.serialized());check(s.solve().status!=SolveStatus::Conflicting,"External tangent segment reopen failed");verify(s);
        auto measured=s;auto reference=*std::ranges::find(measured.dimensions,size.id,&SketchDimension::id);reference.driving=false;reference.locked=false;measured.apply_dimension(reference);
        const auto geometry=measured.points;check(measured.set_dimension_value(size.id,123)&&measured.points==geometry,"Reference R/D drove external tangent geometry");verify(measured);
        if(!fixed_center)for(const auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle,DimensionKind::AngleBetween}) {
            auto edited=s;auto d=kind==DimensionKind::AngleBetween?edited.create_line_pair_dimension("sketch_axis:y",segment,kind):edited.create_segment_dimension(segment,kind);
            edited.apply_dimension(d);

            for(double offset:{.2,-.3,.1}) {
                const bool projected=kind==DimensionKind::DistanceX||kind==DimensionKind::DistanceY;
                const double input=(projected?std::abs(d.value):d.value)+offset;
                const double target=projected?std::copysign(input,d.value):input;
                check(edited.set_dimension_value(d.id,input),"External point tangent segment dimension edit failed");verify(edited);
                const auto* p=edited.find_point(first);const auto* q=edited.find_point(second);
                const double dx=q->x-p->x,dy=q->y-p->y,length=std::hypot(dx,dy);
                const double actual=kind==DimensionKind::Distance?length:kind==DimensionKind::DistanceX?dx:
                    kind==DimensionKind::DistanceY?dy:kind==DimensionKind::Angle?std::atan2(dy,dx)*180/std::acos(-1.):
                    std::acos(std::clamp(dy/length,-1.,1.))*180/std::acos(-1.);
                if(std::abs(kind==DimensionKind::Angle?std::remainder(actual-target,360.):actual-target)>=1e-6)std::cerr<<"equation kind="<<int(kind)<<" actual="<<actual<<" target="<<target<<" reverse="<<reverse<<" side="<<side<<" rotation="<<rotation<<"\n";
                check(std::abs(kind==DimensionKind::Angle?std::remainder(actual-target,360.):actual-target)<1e-6,"External tangent line dimension equation failed");
            }
            const auto before=edited.serialized();auto duplicate=edited.create_segment_dimension(segment,kind);
            if(kind==DimensionKind::AngleBetween)duplicate=edited.create_line_pair_dimension("sketch_axis:y",segment,kind);
            bool rejected=false;try{edited.apply_dimension(duplicate);}catch(const std::exception&){rejected=true;}
            check(rejected&&edited.serialized()==before,"Redundant external tangent dimension was not atomic");
        }
        if(fixed_center) {
            const auto before=s.serialized();check(!s.set_dimension_value(size.id,61*(diameter?2:1))&&s.serialized()==before,"Impossible tangent radius was not atomic");
        }
        ++cases;
    }
    std::cout<<"External point to native circle C+T: "<<cases<<" R/D, contact sides, selection orders, orientations and lock states passed\n";
}
