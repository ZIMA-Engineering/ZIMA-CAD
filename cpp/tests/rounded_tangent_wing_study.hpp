#pragma once
#include <zima/sketcher/sketch.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace rounded_tangent_wing_study {
using namespace zima::sketcher;
inline void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
inline void equations(const Sketch& s) {
    const auto point=[&](const std::string& id) { const auto* p=s.find_point(id);check(p,"Rounded wing lost point identity");return std::array{p->x,p->y}; };
    const auto line=[&](const std::string& id)->std::array<double,4> {
        if(id=="sketch_axis:x")return {0,0,1,0};
        if(id=="sketch_axis:y")return {0,0,0,1};
        const auto segment=std::ranges::find(s.segments,id,&SketchSegment::id);
        if(segment!=s.segments.end()) {const auto a=point(segment->first_point_id),b=point(segment->second_point_id);return {a[0],a[1],b[0]-a[0],b[1]-a[1]};}
        const auto ref=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
        check(ref!=s.external_references.end(),"Rounded wing lost support identity");
        const auto l=external_reference_line(*ref);check(l.has_value(),"Rounded wing lost external line");
        return {l->first[0],l->first[1],l->second[0],l->second[1]};
    };
    const auto& arc=s.arcs.front();const auto center=point(arc.center_point_id);
    for(const auto& id:{arc.start_point_id,arc.end_point_id}) {const auto p=point(id);check(std::abs(std::hypot(p[0]-center[0],p[1]-center[1])-arc.radius)<1e-7,"Rounded wing arc incidence changed");}
    for(const auto& c:s.constraints) {
        if(c.suppressed)continue;
        const auto p=point(c.first_point_id);
        if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical) {
            const auto q=point(c.second_point_id);const int axis=c.kind==ConstraintKind::Horizontal?1:0;
            check(std::abs(p[axis]-q[axis])<1e-7,"Rounded wing H/V equation changed");
        } else if(c.kind==ConstraintKind::PointOnLine) {
            const auto l=line(c.geometry_id);check(std::abs((p[0]-l[0])*l[3]-(p[1]-l[1])*l[2])/std::hypot(l[2],l[3])<1e-7,"Rounded wing line contact changed");
        } else if(c.kind==ConstraintKind::PointOnCircle) {
            check(std::abs(std::hypot(p[0]-center[0],p[1]-center[1])-arc.radius)<1e-7,"Rounded wing circle contact changed");
        } else if(c.kind==ConstraintKind::Tangent) {
            const auto l=line(c.geometry_id==arc.id?c.second_geometry_id:c.geometry_id);
            check(std::abs((p[0]-center[0])*l[2]+(p[1]-center[1])*l[3])/std::hypot(l[2],l[3])<1e-7,"Rounded wing tangent equation changed");
        } else throw std::runtime_error("Unverified rounded wing equation");
    }
    for(const auto& d:s.dimensions) {
        if(d.suppressed)continue;
        double measured{};
        if(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY)measured=point(d.first_point_id)[d.kind==DimensionKind::DistanceX?0:1];
        else if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter) {
            measured=d.geometry_id==arc.id?arc.radius:s.corner_radii.front().radius;
            if(d.kind==DimensionKind::Diameter)measured*=2;
        } else if(d.kind==DimensionKind::AngleBetween) {
            const auto a=line(d.geometry_id),b=line(d.second_geometry_id);
            measured=std::acos(std::clamp((a[2]*b[2]+a[3]*b[3])/(std::hypot(a[2],a[3])*std::hypot(b[2],b[3])),-1.,1.))*180/std::numbers::pi;
        } else throw std::runtime_error("Unverified rounded wing dimension");
        check(std::abs(measured-d.value)<1e-7,"Rounded wing independent dimension equation changed");
    }
    const auto materialized=s.evaluated_profile_sketch();
    check(materialized.arcs.size()==2,"Rounded wing corner did not materialize");
    const auto corner=std::ranges::find(materialized.arcs,s.corner_radii.front().id,&SketchArc::id);
    check(corner!=materialized.arcs.end()&&std::abs(corner->radius-s.corner_radii.front().radius)<1e-8,"Rounded wing lost authored corner radius");
    auto solved=s;const auto status=solved.solve();check(status.maximum_residual<1e-8&&status.status!=SolveStatus::Conflicting&&status.status!=SolveStatus::Invalid,"Rounded wing does not solve after action");
}
inline void run() {
    nlohmann::json fixture;std::ifstream input(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/rounded-tangent-wing.json");input>>fixture;
    std::size_t count=0;
    for(bool swap:{false,true})for(double sx:{-1.,1.})for(double sy:{-1.,1.})
    for(bool reverse:{false,true})for(bool external:{false,true})for(bool locked:{false,true})
    for(bool full:{false,true})for(bool driving:{false,true})
    for(const std::string driver:{"root-x","root-y","height","span","crown-radius","flank-angle","corner-radius"}) {
        const auto label=driver+" swap="+std::to_string(swap)+" sx="+std::to_string(sx)+" sy="+std::to_string(sy)+" order="+std::to_string(reverse)+" external="+std::to_string(external)+" locked="+std::to_string(locked)+" full="+std::to_string(full)+" driving="+std::to_string(driving);
        try {
            auto s=Sketch::from_serialized_json(fixture);
            const auto transform=[&](double x,double y){return swap?std::array{sy*y,sx*x}:std::array{sx*x,sy*y};};
            for(auto& p:s.points){const auto value=transform(p.x,p.y);p.x=value[0];p.y=value[1];}
            for(auto& a:s.arcs) {
                if(sx*sy*(swap?-1:1)<0)std::swap(a.start_point_id,a.end_point_id);
                const auto* c=s.find_point(a.center_point_id);const auto* p=s.find_point(a.start_point_id);const auto* q=s.find_point(a.end_point_id);
                a.start_angle=std::atan2(p->y-c->y,p->x-c->x);a.end_angle=std::atan2(q->y-c->y,q->x-c->x);
                while(a.end_angle<=a.start_angle)a.end_angle+=2*std::numbers::pi;
            }
            const auto axis=[&](std::string& id){if(!swap)return;if(id=="sketch_axis:x")id="sketch_axis:y";else if(id=="sketch_axis:y")id="sketch_axis:x";};
            for(auto& c:s.constraints) {
                axis(c.geometry_id);axis(c.second_geometry_id);
                if(swap){if(c.kind==ConstraintKind::Horizontal)c.kind=ConstraintKind::Vertical;else if(c.kind==ConstraintKind::Vertical)c.kind=ConstraintKind::Horizontal;}
                if(reverse&&c.kind==ConstraintKind::Tangent)std::swap(c.geometry_id,c.second_geometry_id);
            }
            for(auto& d:s.dimensions) {
                axis(d.geometry_id);axis(d.second_geometry_id);d.locked=locked;
                if(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY) {
                    if(swap)d.kind=d.kind==DimensionKind::DistanceX?DimensionKind::DistanceY:DimensionKind::DistanceX;
                    const auto* p=s.find_point(d.first_point_id);d.value=d.kind==DimensionKind::DistanceX?p->x:p->y;
                } else if(d.kind==DimensionKind::AngleBetween) {
                    const auto& l=s.segments.front();const auto* p=s.find_point(l.first_point_id);const auto* q=s.find_point(l.second_point_id);
                    d.value=std::acos(std::clamp((swap?q->y-p->y:q->x-p->x)/std::hypot(q->x-p->x,q->y-p->y),-1.,1.))*180/std::numbers::pi;
                }
            }
            if(reverse)std::ranges::reverse(s.constraints);
            if(external) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.id="read-only-seating-line";ref.source_document_id="read-only-part";ref.source_owner_id="seating";ref.source_semantic_key="edge";
                ref.cached_points={transform(-20,0),transform(60,0)};s.external_references.push_back(ref);
                auto& c=*std::ranges::find(s.constraints,std::string("base-contact"),&SketchConstraint::id);c.geometry_id=ref.id;
            }
            if(!full)std::erase_if(s.dimensions,[](const auto& d){return d.id=="landing-x";});
            auto d=*std::ranges::find(s.dimensions,driver,&SketchDimension::id);d.driving=driving;d.locked=locked&&driving;s.apply_dimension(d);
            const auto initial=d.value;const auto sources=s.external_references;equations(s);
            const auto status=s.solve();check(status.remaining_degrees_of_freedom==(!full)+(!driving),"Rounded wing DOF differs from independent equation count");
            for(double scale:{1.02,1.04,1.}) {
                const auto before=s.serialized();check(s.set_dimension_value(driver,initial*scale),"Rounded wing driver edit was rejected");equations(s);
                if(!driving)check(s.serialized()==before,"Reference dimension edited rounded wing geometry");
                check(s.external_references==sources,"Rounded wing changed a read-only source");
                s=Sketch::from_serialized(s.serialized());equations(s);
            }
            for(double delta:{.05,-.05,.1}) {
                const auto before=s.serialized();const auto previous=*s.find_point("landing");const auto movement=transform(delta,0);
                const bool moved=s.move_point("landing",previous.x+movement[0],previous.y+movement[1]);
                if(!locked||!full)check(moved&&std::hypot(s.find_point("landing")->x-previous.x,s.find_point("landing")->y-previous.y)>1e-6,"Free rounded wing landing did not move");
                else check(!moved&&s.serialized()==before,"Locked rounded wing landing escaped its dimensions");
                equations(s);check(s.external_references==sources,"Rounded wing drag changed a read-only source");
            }
            if(driver!="corner-radius"||driving) {
                auto invalid=s;const auto before=invalid.serialized();check(!invalid.set_dimension_value("corner-radius",10000)&&invalid.serialized()==before,"Impossible rounded wing radius changed the document");
            }
            ++count;
        }catch(const std::exception& error){throw std::runtime_error(label+": "+error.what());}
    }
    std::cout<<count<<" rounded tangent wing variants: repeated coordinate/R/angle edits, both selection orders, transformed native/external supports, lock/reference/DOF states, drags and serialized reopen passed\n";
}
} // namespace rounded_tangent_wing_study
