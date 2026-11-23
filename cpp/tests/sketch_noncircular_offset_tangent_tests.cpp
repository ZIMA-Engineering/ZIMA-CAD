#include <zima/sketcher/curve_geometry.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include "../modules/sketcher/src/derived_curve_contact.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace zima::sketcher;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void equations(const Sketch& s,const std::string& offset,const std::string& circle_id) {
    const auto circle=std::ranges::find(s.circles,circle_id,&SketchCircle::id);
    const auto arc=std::ranges::find(s.arcs,circle_id,&SketchArc::id);
    const auto* center=s.find_point(circle!=s.circles.end()?circle->center_point_id:arc->center_point_id);
    const double radius=circle!=s.circles.end()?circle->radius:arc->radius;
    const auto relation=std::ranges::find_if(s.constraints,[&](const auto& c) {
        return c.kind==ConstraintKind::Tangent&&c.geometry_id==offset;
    });
    check(relation!=s.constraints.end(),"No tangent relation");const auto& tangent=*relation;
    const auto* contact=s.find_point(tangent.first_point_id);check(contact,"No real contact");
    const auto projection=zima::kernel::project_bspline(sketch_curve_geometry(s,offset),{contact->x,contact->y,0});
    const double length=std::hypot(projection.tangent.x,projection.tangent.y);
    if(projection.squared_distance>=1e-14) {
        const auto live=*detail::derived_curve_geometry(s,offset);const auto q=zima::kernel::project_bspline(live,{contact->x,contact->y,0});
        const auto fast=detail::project_derived_curve(live,{contact->x,contact->y,0});
        std::cerr<<"Contact error "<<projection.squared_distance<<" live "<<q.squared_distance<<" fast "<<fast.squared_distance<<" at "<<contact->x<<','<<contact->y<<'\n';
        for(const auto& e:s.ellipses)std::cerr.precision(17),std::cerr<<"Ellipse "<<e.major_radius<<','<<e.minor_radius<<" angle "<<e.rotation<<'\n';
    }
    check(projection.squared_distance<1e-14,"Contact left exact offset");
    check(std::abs(std::hypot(center->x-contact->x,center->y-contact->y)-radius)<1e-7,"Contact left circle");
    check(std::abs((center->x-contact->x)*projection.tangent.x+(center->y-contact->y)*projection.tangent.y)/length<1e-7,"Tangents not parallel");
    const double side=(-(center->x-contact->x)*projection.tangent.y+(center->y-contact->y)*projection.tangent.x);
    check((side<0)==tangent.tangent_internal,"Persisted contact side changed");
    const auto target_projection=s.project_point_to_curve(circle_id,contact->x,contact->y);
    check(target_projection&&std::hypot((*target_projection)[0]-contact->x,(*target_projection)[1]-contact->y)<1e-7,"Contact left finite target domain");
}
}
int main(int argc,char**){try {
    const auto started=std::chrono::steady_clock::now();
    for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto id=s.add_ellipse(2,3,17,3,2,13);
        const auto center=s.ellipses.front().center_point_id;
        check(s.solve().remaining_degrees_of_freedom==5,"Native ellipse has an extra axis degree of freedom");
        auto major=s.create_ellipse_radius_dimension(id,true),minor=s.create_ellipse_radius_dimension(id,false);
        auto rotation=s.create_ellipse_rotation_dimension(id);
        auto x=s.create_axis_dimension(center,"sketch_axis:y"),y=s.create_axis_dimension(center,"sketch_axis:x");
        unsigned remaining=5;
        for(auto dimension:{reverse?minor:major,reverse?major:minor,rotation,x,y}) {
            dimension.locked=true;s.apply_dimension(dimension);
            check(s.solve().remaining_degrees_of_freedom==--remaining,"Ellipse dimension did not remove one actual degree of freedom");
        }
        for(double angle:{30.,-20.,0.}) {
            check(s.set_dimension_value(rotation.id,angle),"Fully dimensioned ellipse angle edit failed");
            const auto& e=s.ellipses.front();const auto* a=s.find_point(e.major_point_id);const auto* b=s.find_point(e.minor_point_id);
            check(std::abs(std::hypot(a->x-2,a->y-3)-15)<1e-7&&std::abs(std::hypot(b->x-2,b->y-3)-10)<1e-7&&
                std::abs((a->x-2)*(b->x-2)+(a->y-3)*(b->y-3))<1e-7,"Ellipse axes violated their dimensions or perpendicularity");
            check(std::abs(std::atan2(a->y-3,a->x-2)*180/std::acos(-1.)-angle)<1e-7,"Ellipse rotation did not reach the driven angle");
            const auto before=s.serialized();check(!s.move_point(e.major_point_id,20,10)&&s.serialized()==before,"Fully dimensioned ellipse escaped by dragging");
            s=Sketch::from_serialized(before);check(s.solve().remaining_degrees_of_freedom==0,"Reopened ellipse changed its rank");
        }
    }
    if(argc==1)for(int kind=0;kind<3;++kind)for(bool reverse:{false,true})for(bool flipped:{false,true}) {
        std::cout<<"case "<<kind<<' '<<reverse<<' '<<flipped<<std::endl;
        auto s=Sketch::create_default();
        const auto source=kind==0?s.add_segment(-20,0,20,0):kind==1?s.add_ellipse(0,0,15,0,0,10):
            s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        for(auto& p:s.points)p.fixed=true;
        const auto offset=s.add_offset(source,2,flipped);
        const auto curve=s.supporting_curve(offset);const auto p=zima::kernel::bspline_value(curve,.4);
        const auto d=zima::kernel::bspline_derivative(curve,.4);const double length=std::hypot(d.x,d.y);
        const auto circle=s.add_circle(p.x-3.2*d.y/length,p.y+3.2*d.x/length,3.);
        static_cast<void>(s.add_tangent_constraint(reverse?circle:offset,reverse?offset:circle));
        equations(s,offset,circle);
        auto dim=reverse?s.create_circle_diameter_dimension(circle):s.create_circle_radius_dimension(circle);
        dim.locked=flipped;s.apply_dimension(dim);
        auto measurement=reverse?s.create_circle_radius_dimension(circle):s.create_circle_diameter_dimension(circle);
        measurement.driving=false;s.apply_dimension(measurement);
        for(double radius:{2.5,3.5,3.}) {
            check(s.set_dimension_value(dim.id,reverse?2*radius:radius),"Radius edit rejected");equations(s,offset,circle);
            check(std::abs(std::ranges::find(s.dimensions,measurement.id,&SketchDimension::id)->value-(reverse?radius:2*radius))<1e-7,"Reference size became stale");
        }
        for(double distance:{1.5,2.5,2.}){s.update_offset(offset,distance,flipped);equations(s,offset,circle);}
        const auto center_id=std::ranges::find(s.circles,circle,&SketchCircle::id)->center_point_id;
        for(double parameter:{.42,.38,.41}) {
            const auto live=sketch_curve_geometry(s,offset);const auto q=zima::kernel::bspline_value(live,parameter);
            const auto t=zima::kernel::bspline_derivative(live,parameter);const double len=std::hypot(t.x,t.y);
            const auto before=*s.find_point(center_id);
            check(s.move_point(center_id,q.x-3*t.y/len,q.y+3*t.x/len),"Center drag rejected");
            check(std::hypot(s.find_point(center_id)->x-before.x,s.find_point(center_id)->y-before.y)>1e-5,"Free center did not move");
            equations(s,offset,circle);
        }
        equations(Sketch::from_serialized(s.serialized()),offset,circle);
    }
    if(argc==1)for(bool arc_target:{false,true})for(bool reverse:{false,true})for(bool negative:{false,true}) {
        std::cout<<"existing contact "<<arc_target<<' '<<reverse<<' '<<negative<<std::endl;
        auto s=Sketch::create_default();const auto source=s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        for(auto& p:s.points)p.fixed=true;
        const auto offset=s.add_offset(source,2,false);const auto curve=sketch_curve_geometry(s,offset);
        const auto p=zima::kernel::bspline_value(curve,.4),t=zima::kernel::bspline_derivative(curve,.4);
        const double len=std::hypot(t.x,t.y),side=negative?-1.:1.;
        const double cx=p.x-side*2.8*t.y/len+std::sqrt(9-2.8*2.8)*t.x/len;
        const double cy=p.y+side*2.8*t.x/len+std::sqrt(9-2.8*2.8)*t.y/len;
        const double angle=std::atan2(p.y-cy,p.x-cx);
        const auto target=arc_target?s.add_arc(cx,cy,p.x,p.y,cx+3*std::cos(angle+1.),cy+3*std::sin(angle+1.)):
            s.add_circle(cx,cy,3.);
        std::string contact;
        if(arc_target)contact=s.arcs.back().start_point_id;
        else {auto q=Sketch::create_point(p.x,p.y);contact=q.id;s.points.push_back(q);}
        static_cast<void>(s.add_point_on_circle_constraint(contact,offset));
        if(!arc_target)static_cast<void>(s.add_point_on_circle_constraint(contact,target));
        const auto point_count=s.points.size(),constraint_count=s.constraints.size();
        const auto tangent=s.add_tangent_constraint(reverse?target:offset,reverse?offset:target);
        check(s.points.size()==point_count&&s.constraints.size()==constraint_count+1,"Existing C junction duplicated");
        check(std::ranges::find(s.constraints,tangent,&SketchConstraint::id)->first_point_id==contact,"Contact identity changed");
        equations(s,offset,target);
        auto size=arc_target?(reverse?s.create_arc_diameter_dimension(target):s.create_arc_radius_dimension(target)):
            (reverse?s.create_circle_diameter_dimension(target):s.create_circle_radius_dimension(target));size.locked=negative;s.apply_dimension(size);
        for(double radius:{2.5,3.5,3.}){check(s.set_dimension_value(size.id,reverse?2*radius:radius),"Existing contact size edit rejected");equations(s,offset,target);}
        const auto center=arc_target?s.arcs.back().center_point_id:s.circles.back().center_point_id;
        for(const auto axis:{"sketch_axis:x","sketch_axis:y"}) {
            auto trial=s;auto coordinate=trial.create_axis_dimension(center,axis);coordinate.locked=negative;trial.apply_dimension(coordinate);
            for(double delta:{.05,-.05,0.}) {
                check(trial.set_dimension_value(coordinate.id,coordinate.value+delta),"Tangent center coordinate edit rejected");
                equations(trial,offset,target);
                check(std::abs(trial.create_axis_dimension(center,axis).value-coordinate.value-delta)<1e-7,"Tangent center missed driven coordinate");
            }
        }
        for(const auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY}) {
            auto trial=s;auto anchor=Sketch::create_point(-25,-15);anchor.fixed=true;trial.points.push_back(anchor);
            auto distance=trial.create_point_dimension(reverse?center:anchor.id,reverse?anchor.id:center,kind);
            distance.locked=negative;trial.apply_dimension(distance);
            // Value entry consumes the displayed magnitude. The signed native
            // X/Y difference retains selection order; negative user input
            // intentionally requests the opposite side instead of a small edit.
            const double displayed=dimension_display_value(distance,trial.plane);
            for(double delta:{.03,-.03,0.}) {
                if(!trial.set_dimension_value(distance.id,displayed+delta)) {
                    std::cerr<<"Distance kind="<<int(kind)<<" value="<<distance.value<<" delta="<<delta<<'\n';
                    throw std::runtime_error("Tangent point-distance edit rejected");
                }
                equations(trial,offset,target);
                const auto measured=trial.create_point_dimension(reverse?center:anchor.id,reverse?anchor.id:center,kind);
                check(std::abs(dimension_display_value(measured,trial.plane)-displayed-delta)<1e-7,
                    "Tangent point distance missed its driven value");
                check(std::signbit(measured.value)==std::signbit(distance.value),"Tangent distance changed its persisted side");
            }
            if(kind!=DimensionKind::Distance) {
                const auto before=trial.serialized();
                check(!trial.set_dimension_value(distance.id,-displayed)&&trial.serialized()==before,
                    "Unreachable opposite-side distance changed the document");
            }
        }
        for(int symmetric:{0,1,2}) {
            auto trial=s;const auto line=trial.add_segment(-30,-10,30,8,true);
            const auto edge=trial.segments.back();trial.find_point(edge.first_point_id)->fixed=true;trial.find_point(edge.second_point_id)->fixed=true;
            std::string partner;
            if(symmetric==2) {
                const auto p=*trial.find_point(center);const double along=((p.x+30)*60+(p.y+10)*18)/(60*60+18*18);
                auto q=Sketch::create_point(2*(-30+60*along)-p.x,2*(-10+18*along)-p.y);partner=q.id;trial.points.push_back(q);
            }
            auto dimension=symmetric?trial.create_symmetric_dimension(center,partner,line):trial.create_point_line_dimension(center,line);
            dimension.locked=negative;trial.apply_dimension(dimension);
            for(double delta:{.03,-.03,0.}) {
                check(trial.set_dimension_value(dimension.id,dimension.value+delta),"Tangent point-line/symmetric edit rejected");
                equations(trial,offset,target);
                const auto measured=trial.create_point_line_dimension(center,line);
                check(std::abs(measured.value*(symmetric?2:1)-dimension.value-delta)<1e-7&&measured.solution_side==dimension.solution_side,
                    "Tangent point-line/symmetric value or side changed");
                if(!partner.empty()) {
                    const auto other=trial.create_point_line_dimension(partner,line);
                    check(std::abs(other.value-measured.value)<1e-7&&other.solution_side==-measured.solution_side,"Symmetric partner lost the opposite side");
                }
            }
        }
        if(arc_target) {
            const auto arc=s.arcs.back();
            for(const auto& point:{arc.center_point_id,arc.start_point_id,arc.end_point_id}) {
                auto trial=s;
                for(double delta:{.1,-.15,.05}) {
                    const auto before=*trial.find_point(point);
                    check(trial.move_point(point,before.x+delta,before.y+.03),"Tangent arc point drag rejected");
                    check(std::hypot(trial.find_point(point)->x-before.x,trial.find_point(point)->y-before.y)>1e-5,"Tangent arc point did not move");
                    equations(trial,offset,target);
                }
            }
            auto trial=s;auto angle=trial.create_three_point_angle_dimension(arc.start_point_id,arc.center_point_id,arc.end_point_id);
            angle.locked=negative;trial.apply_dimension(angle);
            for(double delta:{1.,-1.,0.}) {
                check(trial.set_dimension_value(angle.id,angle.value+delta),"Tangent arc angle edit rejected");equations(trial,offset,target);
                check(std::abs(trial.create_three_point_angle_dimension(arc.start_point_id,arc.center_point_id,arc.end_point_id).value-angle.value-delta)<1e-7,
                    "Tangent arc missed its driven angle");
            }
        }
        // A later trim retaining the contact must preserve its C/T identity.
        auto trimmed=s;
        const auto cp=trimmed.find_point(contact);
        const auto parameter=zima::kernel::project_bspline(sketch_curve_geometry(trimmed,offset),{cp->x,cp->y,0}).parameter;
        const auto removed_point=zima::kernel::bspline_value(sketch_curve_geometry(trimmed,offset),parameter<.5?(parameter+1)*.5:parameter*.5);
        const auto pieces=sketch_trim_topology(trimmed,false);
        const auto removed=nearest_sketch_trim_piece(pieces,{removed_point.x,removed_point.y},.01);
        check(removed&&removed->geometry_id==offset,"Tangent contact did not split Offset for trimming");
        static_cast<void>(apply_sketch_trim(trimmed,{*removed}));
        equations(trimmed,offset,target);
        check(trimmed.find_point(contact)&&std::ranges::find(trimmed.constraints,tangent,&SketchConstraint::id)->first_point_id==contact,
            "Dependent Offset trim changed tangent contact identity");
        equations(Sketch::from_serialized(trimmed.serialized()),offset,target);
        if(!arc_target) {
            auto trimmed_target=s;const auto* c=s.find_point(center);const auto* p=s.find_point(contact);
            auto opposite=Sketch::create_point(2*c->x-p->x,2*c->y-p->y);trimmed_target.points.push_back(opposite);
            static_cast<void>(trimmed_target.add_point_on_circle_constraint(opposite.id,target));
            const auto pieces=sketch_trim_topology(trimmed_target,false);
            const auto removed=nearest_sketch_trim_piece(pieces,{c->x-(p->y-c->y),c->y+(p->x-c->x)},.01);
            check(removed&&removed->geometry_id==target,"Tangent circle was not divided for trimming");
            static_cast<void>(apply_sketch_trim(trimmed_target,{*removed}));
            check(trimmed_target.circles.empty()&&trimmed_target.arcs.size()==1,"Tangent circle did not become a native arc");
            equations(trimmed_target,offset,target);
            for(double radius:{2.8,3.2,3.}) {
                check(trimmed_target.set_dimension_value(size.id,reverse?2*radius:radius),"Trimmed tangent arc size edit rejected");
                equations(trimmed_target,offset,target);
            }
            equations(Sketch::from_serialized(trimmed_target.serialized()),offset,target);
        }
        const auto before=s.serialized();bool rejected{};
        try{static_cast<void>(s.add_tangent_constraint(target,offset));}catch(const std::exception&){rejected=true;}
        check(rejected&&s.serialized()==before,"Duplicate T was not atomic");
        s=Sketch::from_serialized(before);equations(s,offset,target);
        s.remove_constraint(tangent);check(s.points.size()==point_count&&s.find_point(contact),"Removing T removed contact");
        check(s.constraints.size()==constraint_count,"Removing T removed C");
    }
    if(argc==1)for(bool reverse:{false,true})for(bool trimmed:{false,true}) {
        std::cout<<"external "<<reverse<<' '<<trimmed<<std::endl;
        auto original=Sketch::create_default();const auto native=original.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        auto s=Sketch::create_default();auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        reference.source_document_id="external-fixture";reference.source_owner_id="source";reference.source_semantic_key="bspline";
        reference.exact_spline=sketch_curve_geometry(original,native);
        for(unsigned i=0;i<=32;++i){const auto p=zima::kernel::bspline_value(*reference.exact_spline,i/32.);reference.cached_points.push_back({p.x,p.y});}
        s.add_external_reference(reference);const auto source=s.add_external_profile_geometry(reference.id);
        const auto offset=s.add_offset(source,2,false);
        if(trimmed)static_cast<void>(s.retain_curve_intervals(offset,{{.15,.85}}));
        const auto curve=sketch_curve_geometry(s,offset);const auto p=zima::kernel::bspline_value(curve,.4),t=zima::kernel::bspline_derivative(curve,.4);
        const double len=std::hypot(t.x,t.y);const auto target=s.add_circle(p.x-3.2*t.y/len,p.y+3.2*t.x/len,3);
        std::vector<SketchPoint> readonly;for(const auto& id:s.import_blocks.front().point_ids)readonly.push_back(*s.find_point(id));
        static_cast<void>(s.add_tangent_constraint(reverse?target:offset,reverse?offset:target));equations(s,offset,target);
        auto size=s.create_circle_radius_dimension(target);size.locked=true;s.apply_dimension(size);
        for(double radius:{2.5,3.5,3.}) {
            check(s.set_dimension_value(size.id,radius),"External size edit rejected");equations(s,offset,target);
            check(s.external_references.front()==reference,"External reference mutated");
            for(const auto& q:readonly)check(*s.find_point(q.id)==q,"Read-only source point mutated");
        }
        equations(Sketch::from_serialized(s.serialized()),offset,target);
    }
    if(argc==1)for(int dimension=0;dimension<3;++dimension) {
        std::cout<<"source ellipse dimension "<<dimension<<std::endl;
        auto s=Sketch::create_default();const auto source=s.add_ellipse(0,0,15,0,0,10);
        s.find_point(s.ellipses.front().center_point_id)->fixed=true;
        const auto major=s.create_ellipse_radius_dimension(source,true),minor=s.create_ellipse_radius_dimension(source,false),rotation=s.create_ellipse_rotation_dimension(source);
        s.apply_dimension(major);s.apply_dimension(minor);s.apply_dimension(rotation);
        const auto offset=s.add_offset(source,2,false);const auto curve=sketch_curve_geometry(s,offset);
        const auto p=zima::kernel::bspline_value(curve,.4),t=zima::kernel::bspline_derivative(curve,.4);const double len=std::hypot(t.x,t.y);
        const auto target=s.add_circle(p.x-3.2*t.y/len,p.y+3.2*t.x/len,3);
        static_cast<void>(s.add_tangent_constraint(offset,target));equations(s,offset,target);
        auto size=s.create_circle_radius_dimension(target);s.apply_dimension(size);
        const auto& d=dimension==0?major:dimension==1?minor:rotation;
        for(double delta:{.5,-.5,0.}) {
            std::cout<<"  delta "<<delta<<std::endl;
            check(s.set_dimension_value(d.id,d.value+delta),"Source ellipse dimension rejected");equations(s,offset,target);
            const auto& e=s.ellipses.front();check(std::abs(e.major_radius-(dimension==0?15+delta:15))<1e-7&&
                std::abs(e.minor_radius-(dimension==1?10+delta:10))<1e-7&&std::abs(e.rotation*180/std::acos(-1.)-(dimension==2?delta:0))<1e-7,"Source ellipse equation changed");
        }
        if(dimension==0)for(double radius:{10.,15.,10.,15.}) {
            std::cout<<"  equal-axis radius "<<radius<<std::endl;
            check(s.set_dimension_value(major.id,radius),"Equal-semiaxis transition rejected");equations(s,offset,target);
            s=Sketch::from_serialized(s.serialized());equations(s,offset,target);
        }
        equations(Sketch::from_serialized(s.serialized()),offset,target);
    }
    if(argc==1)for(bool chained:{false,true}) {
        std::cout<<"free source and chain "<<chained<<std::endl;
        auto s=Sketch::create_default();const auto source=chained?s.add_segment(-20,0,20,0):s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        const auto pole=chained?s.segments.front().second_point_id:s.bsplines.front().control_point_ids[1];for(auto& p:s.points)p.fixed=p.id!=pole;
        const auto first=s.add_offset(source,2,false);
        const auto offset=chained?s.add_offset(first,1,true):first;
        const auto curve=sketch_curve_geometry(s,offset);const auto p=zima::kernel::bspline_value(curve,.4),t=zima::kernel::bspline_derivative(curve,.4);const double len=std::hypot(t.x,t.y);
        const auto target=s.add_circle(p.x-3.2*t.y/len,p.y+3.2*t.x/len,3);
        static_cast<void>(s.add_tangent_constraint(offset,target));equations(s,offset,target);
        s.apply_dimension(s.create_circle_radius_dimension(target));
        for(double delta:{.5,-.5,0.}) {
            const auto before=*s.find_point(pole);check(s.move_point(pole,chained?20:-8,(chained?0:8)+delta),"Source pole drag rejected");
            check(std::hypot(s.find_point(pole)->x-before.x,s.find_point(pole)->y-before.y)>1e-5,"Source pole did not move");equations(s,offset,target);
        }
        for(bool flipped:{true,false,true}){s.update_offset(first,2,flipped);equations(s,offset,target);}
        equations(Sketch::from_serialized(s.serialized()),offset,target);
    }
    {
        // Existing Offset generation rejects this double nonlinear offset
        // before a tangent can be requested. Keep that precision gate atomic.
        auto s=Sketch::create_default();const auto source=s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        const auto first=s.add_offset(source,2,false);const auto before=s.serialized();bool rejected{};
        try{static_cast<void>(s.add_offset(first,1,true));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&s.serialized()==before,"Unsupported nonlinear chain did not reject atomically");
    }
    {
        std::cout<<"fully constrained and rejection"<<std::endl;
        auto s=Sketch::create_default();const auto source=s.add_segment(-20,0,20,0);for(auto& p:s.points)p.fixed=true;
        const auto offset=s.add_offset(source,2,false),target=s.add_circle(0,5.2,3);
        static_cast<void>(s.add_tangent_constraint(offset,target));const auto center=s.circles.front().center_point_id;
        auto size=s.create_circle_radius_dimension(target);size.locked=true;s.apply_dimension(size);
        auto x=s.create_axis_dimension(center,"sketch_axis:y");x.locked=true;s.apply_dimension(x);
        check(s.solve().remaining_degrees_of_freedom==0,"Circle is not fully constrained");
        const auto before=s.serialized();check(!s.move_point(center,2,8)&&s.serialized()==before,"Fully constrained circle escaped");
        s.find_point(center)->fixed=true;const auto fixed=s.serialized();
        check(!s.set_dimension_value(size.id,4)&&s.serialized()==fixed,"Impossible radius edit changed document");
        auto separate=Sketch::create_default();const auto line=separate.add_segment(-20,0,20,0);const auto off=separate.add_offset(line,2,false),circle=separate.add_circle(0,5,3);
        const auto original=separate.serialized();bool rejected{};
        try{static_cast<void>(separate.add_tangent_constraint(off,circle,separate.circles.front().center_point_id));}catch(const std::exception&){rejected=true;}
        check(rejected&&separate.serialized()==original,"Circle center accepted as tangent contact");
    }
    for(bool reverse:{false,true}) {
        std::cout<<"fixed circle with editable ellipse "<<reverse<<std::endl;
        auto s=Sketch::create_default();const auto source=s.add_ellipse(0,0,15,0,0,10);
        s.find_point(s.ellipses.front().center_point_id)->fixed=true;
        const auto offset=s.add_offset(source,2,false);
        const auto target=s.add_circle(16.2,0,3);s.find_point(s.circles.front().center_point_id)->fixed=true;
        auto size=s.create_circle_radius_dimension(target);size.locked=true;s.apply_dimension(size);
        static_cast<void>(s.add_tangent_constraint(reverse?target:offset,reverse?offset:target));equations(s,offset,target);
        const auto free_axes=s.solve();std::cout<<"editable ellipse DOF "<<free_axes.remaining_degrees_of_freedom<<std::endl;
        check(free_axes.remaining_degrees_of_freedom==2,"Editable ellipse tangency has incorrect DOF");
        auto minor=s.create_ellipse_radius_dimension(source,false),rotation=s.create_ellipse_rotation_dimension(source);minor.locked=true;rotation.locked=true;
        s.apply_dimension(minor);s.apply_dimension(rotation);
        check(s.solve().remaining_degrees_of_freedom==0,"Dimensioned ellipse did not become fully constrained");
        const auto before=s.serialized();const auto major=s.ellipses.front().major_point_id;
        check(!s.move_point(major,20,2)&&s.serialized()==before,"Fully constrained source ellipse escaped");
    }
    {
        std::cout<<"exact projector oracle"<<std::endl;
        auto s=Sketch::create_default();const auto ellipse=s.add_ellipse(0,0,15,0,0,10),spline=s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}});
        const auto offset=s.add_offset(ellipse,2,false);const auto other=s.add_offset(spline,1,true);
        detail::DerivedCurveEvaluationScope scope;
        for(const auto& id:{ellipse,spline,offset,other}) {
            const auto curve=sketch_curve_geometry(s,id);
            for(double t:{0.,.125,.25,.4,.5,.75,1.})for(double distance:{0.,.1,4.}) {
                auto p=zima::kernel::bspline_value(curve,t);p.x+=distance;p.y-=distance*.3;
                const auto fast=detail::project_derived_curve(curve,p),ordinary=zima::kernel::project_bspline(curve,p);
                check(std::abs(fast.squared_distance-ordinary.squared_distance)<1e-8,"Conservative projector disagrees with exact oracle");
                check(fast.unique_tangent==ordinary.unique_tangent,"Conservative projector changed tangent uniqueness");
            }
        }
        const zima::kernel::BSplineGeometry corner{1,{{-2,0,0},{0,0,0},{0,2,0}},{0,0,.5,1,1},{1,1,1}};
        check(!detail::project_derived_curve(corner,{0,0,0}).unique_tangent,"Corner gained an arbitrary tangent");
        const zima::kernel::BSplineGeometry crossing{1,{{-1,-1,0},{1,1,0},{1,-1,0},{-1,1,0}},{0,0,1./3,2./3,1,1},{1,1,1,1}};
        check(!detail::project_derived_curve(crossing,{0,0,0}).unique_tangent,"Crossing gained an arbitrary tangent");
    }
    std::cout<<"Noncircular Offset contacts passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
