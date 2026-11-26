#include "sketch_mechanism_study.hpp"
#include "double_circle_tangent_study.hpp"
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <chrono>
#include <limits>

using namespace zima;
using namespace zima::sketcher;
using XY = std::array<double, 2>;
namespace {
unsigned checks{}, cases{}, gestures{}, point_drags{}, edits{};
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void near(double a, double b, const std::string& message) {
    check(std::abs(a-b) < 2e-6, message+" actual="+std::to_string(a)+" expected="+std::to_string(b));
}
XY point(const Sketch& s, const std::string& id) {
    const auto* p=s.find_point(id);check(p!=nullptr,"Missing point identity");return {p->x,p->y};
}
XY grip(const Sketch& s, const SketchSegment& l, double u) {
    const auto a=point(s,l.first_point_id),b=point(s,l.second_point_id);
    return {(1-u)*a[0]+u*b[0],(1-u)*a[1]+u*b[1]};
}
double length(const Sketch& s, const SketchSegment& l) {
    const auto a=point(s,l.first_point_id),b=point(s,l.second_point_id);
    return std::hypot(b[0]-a[0],b[1]-a[1]);
}
void contracts(const Sketch& before,const Sketch& after) {
    check(before.points.size()==after.points.size(),"Contour gesture inserted a point");
    check(before.constraints==after.constraints,"Contour gesture changed incidence/side/constraint identity");
    check(before.external_references==after.external_references,"Contour gesture changed read-only supports");
    check(before.dimensions.size()==after.dimensions.size(),"Transient dimension escaped");
    for(std::size_t i=0;i<before.points.size();++i)check(before.points[i].id==after.points[i].id,"Point identity changed");
    for(std::size_t i=0;i<before.dimensions.size();++i) {
        const auto& a=before.dimensions[i];const auto& b=after.dimensions[i];
        check(a.id==b.id&&a.driving==b.driving&&a.locked==b.locked&&a.solution_side==b.solution_side,"Dimension contract changed");
        if(a.driving&&a.locked)near(a.value,b.value,"Locked dimension value changed");
    }
    auto verified=after;const auto result=verified.solve();
    check(result.status!=SolveStatus::Conflicting&&result.status!=SolveStatus::Invalid,"Gesture left invalid equations");
    for(const auto& p:after.points){const auto* q=verified.find_point(p.id);near(p.x,q->x,"Further solve changed X");near(p.y,q->y,"Further solve changed Y");}
    for(const auto& c:after.circles)near(c.radius,*verified.circular_radius(c.id),"Further solve changed radius");
    after.validate();
    check(Sketch::from_serialized(after.serialized()).serialized()==after.serialized(),"Sketch reopen changed gesture");
}
void history(const Sketch& s) {
    auto original=zima::test::mechanism::part(s);
    document::DocumentSession session(original);
    auto edited=original;
    const auto id=edited.sketches.front().segments.front().id;
    const auto l=edited.sketches.front().segments.front();
    const auto from=grip(edited.sketches.front(),l,.7);
    check(edited.sketches.front().drag_contour(id,from,{from[0]+2,from[1]+3}),"Session contour drag failed");
    const auto wanted=edited.sketches.front().serialized();session.commit(edited);
    check(session.undo(),"Contour Undo unavailable");
    check(session.document().sketches.front().serialized()==original.sketches.front().serialized(),"Contour Undo changed source");
    check(session.redo(),"Contour Redo unavailable");
    check(session.document().sketches.front().serialized()==wanted,"Contour Redo changed geometry");
    const auto file=std::filesystem::temp_directory_path()/("zima-contour-"+original.document_id+".prtz");
    session.document().save(file);const auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
    check(loaded.sketches.front().serialized()==wanted,"Native Part reopen changed gesture");
}
void lines() {
    for(double angle:{0.,.47,1.5707963267948966})for(bool reverse:{false,true})
    for(int pivot:{-1,0,1,2})for(bool external:{false,true})for(double u:{.1,.5,.9})for(int state:{0,1,2,3}) {
        auto s=Sketch::create_default();const double vx=20*std::cos(angle),vy=20*std::sin(angle);
        auto id=reverse?s.add_segment(vx,vy,0,0):s.add_segment(0,0,vx,vy);const auto line=s.segments.front();
        const auto pa=point(s,line.first_point_id),pb=point(s,line.second_point_id);
        std::string pin;
        if(pivot>=0) {
            pin=pivot==0?line.first_point_id:pivot==1?line.second_point_id:s.add_point(vx/2,vy/2);
            if(external) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
                ref.source_document_id="ground";ref.source_owner_id="pin";ref.source_semantic_key="point";ref.cached_points={point(s,pin)};
                s.external_references.push_back(ref);static_cast<void>(s.add_point_reference_constraint(pin,ref.id));
            } else s.find_point(pin)->fixed=true;
            if(pivot==2)static_cast<void>(s.add_point_on_line_constraint(pin,id));
        }
        std::string dim;
        if(state) {auto d=s.create_segment_dimension(id);d.driving=state!=1;d.locked=state==3;dim=d.id;s.apply_dimension(d);}
        const auto initial=s;const auto anchor=pin.empty()?XY{}:point(s,pin);
        for(const auto delta:{XY{2,3},XY{-3,2},XY{1,-4}}) {
            auto before=s;const auto from=grip(s,line,u);const XY target{from[0]+delta[0],from[1]+delta[1]};
            const bool moved=s.drag_contour(id,from,target);++gestures;
            if(pivot==2&&u==.5)check(!moved&&s.serialized()==before.serialized(),"Dragging the pin itself changed the lever");
            else check(moved,"Line contour did not move: pivot="+std::to_string(pivot)+" ext="+std::to_string(external)+" angle="+std::to_string(angle)+" u="+std::to_string(u));
            near(length(s,line),20,"Outline stretched the line");
            if(!pin.empty()) {
                const auto current=point(s,pin);near(current[0],anchor[0],"Pin X moved");near(current[1],anchor[1],"Pin Y moved");
                if(pivot==2){const auto a=point(s,line.first_point_id);near(std::hypot(a[0]-anchor[0],a[1]-anchor[1]),10,"Lever arm changed");}
            } else {
                const auto a=point(s,line.first_point_id),old=point(before,line.first_point_id);
                near(a[0]-old[0],delta[0],"Free translation X");near(a[1]-old[1],delta[1],"Free translation Y");
            }
            contracts(before,s);
        }
        for(double value:{22.,18.,21.}) {
            if(!dim.empty()) {const double previous=length(s,line);check(s.set_dimension_value(dim,value),"Line dimension no longer editable");++edits;near(length(s,line),state==1?previous:value,"Edited line length");}
            const auto before=s;
            const auto fixed=pin.empty()?point(s,line.first_point_id):point(s,pin);
            const auto endpoint=pivot==1?line.first_point_id:line.second_point_id;const auto p=point(s,endpoint);
            const double x=p[0]-fixed[0],y=p[1]-fixed[1];
            check(s.move_point(endpoint,fixed[0]+std::cos(.07)*x-std::sin(.07)*y,fixed[1]+std::sin(.07)*x+std::cos(.07)*y),"Endpoint drag after contour/dimension edit failed");++point_drags;contracts(before,s);
        }
        ++cases;
    }
    auto s=Sketch::create_default();static_cast<void>(s.add_segment(0,0,20,0));history(s);
}
void constrained_lines() {
    for(bool vertical:{false,true})for(bool reverse:{false,true})for(int mode:{0,1,2,3,4})for(int state:{0,1,2}) {
        auto s=Sketch::create_default();
        const auto id=vertical?s.add_segment(5,5,5,25):s.add_segment(5,5,25,5);const auto line=s.segments.front();
        static_cast<void>(s.add_segment_constraint(id,vertical?ConstraintKind::Vertical:ConstraintKind::Horizontal));
        SketchDimension dimension;
        if(mode==0)dimension=s.create_segment_dimension(id,vertical?DimensionKind::DistanceY:DimensionKind::DistanceX);
        if(mode==1)dimension=s.create_axis_dimension(line.first_point_id,vertical?"sketch_axis:y":"sketch_axis:x");
        if(mode==2)dimension=s.create_segment_dimension(id,DimensionKind::Angle);
        if(mode==3)dimension=s.create_point_dimension(reverse?line.second_point_id:line.first_point_id,reverse?line.first_point_id:line.second_point_id);
        if(mode==4)dimension=s.create_point_line_dimension(line.first_point_id,vertical?"sketch_axis:y":"sketch_axis:x");
        dimension.driving=state!=0;dimension.locked=state==2;
        // Angle duplicates H/V. Keep it as a reference; the orientation equation
        // is already tested as the driving H/V relation.
        if(mode==2){dimension.driving=false;dimension.locked=false;}
        s.apply_dimension(dimension);
        for(const auto delta:{XY{2,3},XY{-3,2},XY{1,-4}}) {
            const auto before=s;const auto from=grip(s,line,.7);
            check(s.drag_contour(id,from,{from[0]+delta[0],from[1]+delta[1]}),"Axis/coordinate contour has a free DOF but did not move");++gestures;
            const auto a=point(s,line.first_point_id),b=point(s,line.second_point_id);
            const auto old=point(before,line.first_point_id);
            near(vertical?a[1]-old[1]:a[0]-old[0],vertical?delta[1]:delta[0],"Constrained translation missed its free cursor component mode="+std::to_string(mode)+" state="+std::to_string(state)+" vertical="+std::to_string(vertical));
            near(vertical?a[0]:a[1],vertical?b[0]:b[1],"H/V equation lost");near(length(s,line),20,"H/V length changed");
            if((mode==1||mode==4)&&state==2)near(vertical?a[0]:a[1],5,"Locked support distance moved");
            contracts(before,s);
        }
        ++cases;
    }
    for(bool reverse:{false,true})for(bool external:{false,true})for(auto relation:{ConstraintKind::Parallel,ConstraintKind::Perpendicular,ConstraintKind::EqualLength}) {
        // External equality is not an existing segment-pair capability.
        if(external&&relation==ConstraintKind::EqualLength)continue;
        auto s=Sketch::create_default();const auto id=s.add_segment(0,10,20,10);const auto line=s.segments.front();std::string support;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="ground";ref.source_owner_id="line";ref.source_semantic_key="line";
            ref.cached_points=relation==ConstraintKind::Perpendicular?std::vector<XY>{{-10,-5},{-10,15}}:std::vector<XY>{{0,-5},{20,-5}};
            support=ref.id;s.external_references.push_back(ref);
        } else {
            support=relation==ConstraintKind::Perpendicular?s.add_segment(-10,-5,-10,15):s.add_segment(0,-5,20,-5);
            const auto base=s.segments.back();s.find_point(base.first_point_id)->fixed=true;s.find_point(base.second_point_id)->fixed=true;
        }
        // Reference-entry normalizes the read-only support into the first slot.
        static_cast<void>(s.add_segment_pair_constraint(external||!reverse?support:id,external||!reverse?id:support,relation));
        for(const auto delta:{XY{2,3},XY{-3,2},XY{1,-4}}) {
            const auto before=s;const auto from=grip(s,line,.4);check(s.drag_contour(id,from,{from[0]+delta[0],from[1]+delta[1]}),"Related line failed contour translation");++gestures;
            near(length(s,line),20,"Related line length changed");const auto a=point(s,line.first_point_id),b=point(s,line.second_point_id);near(a[1],b[1],"Related line orientation changed");contracts(before,s);
        }
        ++cases;
    }
    for(bool full:{false,true}) {
        auto s=Sketch::create_default();const auto id=s.add_segment(0,0,10,0);const auto line=s.segments.front();s.find_point(line.first_point_id)->fixed=true;
        if(full)s.find_point(line.second_point_id)->fixed=true;else {auto a=s.create_segment_dimension(id,DimensionKind::Angle);a.locked=true;s.apply_dimension(a);}
        const auto before=s.serialized();check(!s.drag_contour(id,{5,0},{7,5})&&s.serialized()==before,"Rigid/full line escaped its equations");
        check(!s.drag_contour(id,{5,0},{5,0})&&s.serialized()==before,"No-op contour changed document");
        check(!s.drag_contour(id,{5,0},{std::numeric_limits<double>::infinity(),0})&&s.serialized()==before,"Nonfinite contour changed document");++cases;
    }
}
void circles() {
    for(bool diameter:{false,true})for(int state:{0,1,2,3})for(int support:{0,1,2})for(int side:{-1,1})for(bool order:{false,true}) {
        auto s=Sketch::create_default();const auto id=s.add_circle(0,side*10,10),center=s.circles.front().center_point_id;
        if(support==1)s.find_point(center)->fixed=true;
        if(support==2)static_cast<void>(s.add_tangent_constraint(order?id:"sketch_axis:x",order?"sketch_axis:x":id));
        std::string dim;
        if(state){auto d=diameter?s.create_circle_diameter_dimension(id):s.create_circle_radius_dimension(id);d.driving=state!=1;d.locked=state==3;dim=d.id;s.apply_dimension(d);}
        for(double requested:{12.,8.,11.}) {
            auto before=s;const auto c=point(s,center);const double old=s.circles.front().radius;
            const bool moved=s.drag_contour(id,{c[0]+old,c[1]},{c[0]+requested,c[1]});++gestures;
            if(state==3)check(!moved&&before.serialized()==s.serialized(),"Locked radius escaped");
            else {check(moved,"Circle contour did not resize");near(s.circles.front().radius,requested,"Circle radius");}
            const auto actual=point(s,center);
            if(support==2)near(actual[1],side*s.circles.front().radius,"Circle lost oriented tangency");
            else {near(actual[0],c[0],"Circle center X moved");near(actual[1],c[1],"Circle center Y moved");}
            contracts(before,s);
            if(!dim.empty())near(s.dimensions.front().value,(diameter?2:1)*s.circles.front().radius,"R/D measurement stale");
        }
        for(double value:{13.,9.,11.}) {
            if(!dim.empty()){check(s.set_dimension_value(dim,diameter?2*value:value),"Radius dimension no longer editable");++edits;}
            const auto before=s;const auto c=point(s,center);const bool moved=s.move_point(center,c[0]+2,c[1]);++point_drags;
            check(moved==(support!=1),"Circle center editability after contour/dimension edit");
            if(support!=1)near(point(s,center)[0],c[0]+2,"Circle center drag displacement");contracts(before,s);
        }
        ++cases;
    }
}
void circle_relations() {
    for(bool reverse:{false,true})for(bool external:{false,true})for(int side:{-1,1}) {
        auto s=Sketch::create_default();const auto id=s.add_circle(0,side*10,10);const auto center=s.circles.front().center_point_id;std::string support;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="ground";ref.source_owner_id="support";ref.source_semantic_key="line";ref.infinite=true;ref.cached_points={{-50,0},{50,0}};
            support=ref.id;s.external_references.push_back(ref);
        } else {
            support=s.add_segment(-50,0,50,0);const auto line=s.segments.front();s.find_point(line.first_point_id)->fixed=true;s.find_point(line.second_point_id)->fixed=true;
        }
        static_cast<void>(s.add_tangent_constraint(reverse?id:support,reverse?support:id));
        for(double radius:{12.,8.,11.}) {
            const auto before=s;const auto c=point(s,center);const double old=s.circles.front().radius;
            check(s.drag_contour(id,{c[0]+old,c[1]},{c[0]+radius,c[1]}),"Native/external tangent contour failed");++gestures;
            near(s.circles.front().radius,radius,"Tangent circle size");near(point(s,center)[1],side*radius,"Tangent side/contact equation");contracts(before,s);
        }
        ++cases;
    }
    for(bool locked:{false,true})for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto a=s.add_circle(0,0,10),b=s.add_circle(30,0,10);
        static_cast<void>(s.add_equal_radius_constraint(reverse?a:b,reverse?b:a));
        if(locked){auto d=s.create_circle_radius_dimension(b);d.locked=true;s.apply_dimension(d);}
        const auto before=s;
        const bool moved=s.drag_contour(a,{10,0},{12,0});++gestures;
        check(moved!=locked,"Equal circle mobility/locked propagation");near(s.circles[0].radius,locked?10:12,"Equal driver radius");near(s.circles[1].radius,s.circles[0].radius,"Equal radii equation");contracts(before,s);++cases;
    }
    auto s=Sketch::create_default();const auto id=s.add_circle(0,0,10);auto d=s.create_circle_diameter_dimension(id);d.upper_limit=22;d.lower_limit=18;s.apply_dimension(d);
    const auto before=s.serialized();for(double r:{8.,12.,0.})check(!s.drag_contour(id,{10,0},{r,0})&&s.serialized()==before,"Circle absolute limit escaped");
}
void mechanisms() {
    using namespace zima::test::mechanism;
    for(auto kind:{Kind::SliderCrank,Kind::FourBar,Kind::MidpointBranch,Kind::Watt,Kind::ScottRussell})
    for(bool reverse:{false,true})for(bool external:{false,true})for(double rotation:{0.,27.}) {
        auto f=make(kind,1,rotation,reverse,external);
        const auto line=*std::ranges::find(f.sketch.segments,f.crank,&SketchSegment::id);
        for(double angle:{38.,30.,34.}) {
            const auto before=f.sketch;const auto from=grip(f.sketch,line,.7);const auto pose=f.pose(angle);
            const auto wanted=std::ranges::find(pose,f.a,[](const auto& p){return p.first;})->second;
            const auto anchor=point(f.sketch,f.o);const double fraction=line.first_point_id==f.o?.7:.3;
            const XY to{anchor[0]+fraction*(wanted[0]-anchor[0]),anchor[1]+fraction*(wanted[1]-anchor[1])};
            ::check(f.sketch.drag_contour(f.crank,from,to),"Mechanism contour did not move");++gestures;
            zima::test::mechanism::check(f,angle);contracts(before,f.sketch);
        }
        ++cases;
    }
}
void circular_contacts() {
    for(bool external:{false,true})for(bool internal:{false,true})for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();std::string support;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="ground";ref.source_owner_id="circle";ref.source_semantic_key="circle";
            for(unsigned n=0;n<=64;++n)ref.cached_points.push_back({30*std::cos(n*std::numbers::pi/32),30*std::sin(n*std::numbers::pi/32)});
            support=ref.id;s.external_references.push_back(ref);
        } else {
            support=s.add_circle(0,0,30);s.find_point(s.circles.back().center_point_id)->fixed=true;
            auto d=s.create_circle_radius_dimension(support);d.locked=true;s.apply_dimension(d);
        }
        const auto id=s.add_circle(internal?20:40,0,10),center=s.circles.back().center_point_id;
        static_cast<void>(s.add_tangent_constraint(reverse?id:support,reverse?support:id));
        for(double r:{11.,9.,12.}) {
            const auto before=s;const auto c=point(s,center);const double old=*s.circular_radius(id);
            check(s.drag_contour(id,{c[0],c[1]+old},{c[0],c[1]+r}),"Circular C/T outline failed");++gestures;
            near(*s.circular_radius(id),r,"Circular C/T radius");test::verify_circle_contacts(s);contracts(before,s);
        }
        ++cases;
    }
    for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto first=s.add_circle(0,0,20),second=s.add_circle(2,3,10);
        static_cast<void>(s.add_concentric_constraint(reverse?second:first,reverse?first:second));
        s.find_point(s.circles.front().center_point_id)->fixed=true;const auto before=s;
        const auto center=point(s,s.circles.back().center_point_id);
        check(s.drag_contour(second,{center[0]+10,center[1]},{center[0]+12,center[1]}),"Concentric outline failed");++gestures;
        near(s.circles[0].radius,20,"Concentric source size changed");near(s.circles[1].radius,12,"Concentric driven size");contracts(before,s);++cases;
    }
}
void dependencies() {
    for(bool circle:{false,true}) {
        auto s=Sketch::create_default();auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        ref.source_document_id="external";ref.source_owner_id="support";ref.source_semantic_key=circle?"circle":"line";
        if(circle)for(unsigned i=0;i<=64;++i)ref.cached_points.push_back({10*std::cos(i*std::numbers::pi/32),10*std::sin(i*std::numbers::pi/32)});
        else ref.cached_points={{0,0},{20,0}};
        s.add_external_reference(ref);const auto id=s.add_external_profile_geometry(ref.id);const auto before=s.serialized();
        check(!s.drag_contour(id,{10,0},{12,3})&&s.serialized()==before,"Read-only external outline changed");++cases;
    }
    for(bool circle:{false,true})for(bool flipped:{false,true}) {
        auto s=Sketch::create_default();const auto id=circle?s.add_circle(0,0,10):s.add_segment(0,0,20,0);
        const auto offset=s.add_offset(id,2,flipped);
        for(double delta:{1.,-2.,3.}) {
            const auto before=s;
            if(circle){const double r=s.circles.front().radius;check(s.drag_contour(id,{r,0},{r+delta,0}),"Offset source circle did not resize");}
            else {const auto p=grip(s,s.segments.front(),.5);check(s.drag_contour(id,p,{p[0]+delta,p[1]+delta}),"Offset source line did not move");}
            ++gestures;
            const auto curve=sketch_curve_geometry(s,offset),source=s.supporting_curve(id);
            for(unsigned i=0;i<=32;++i){const double u=i/32.;const auto actual=kernel::bspline_value(curve,u),expected=curve_offset_point(source,u,flipped?-2:2);
                near(actual.x,expected.x,"Dependent offset X");near(actual.y,expected.y,"Dependent offset Y");}
            contracts(before,s);
        }
        ++cases;
    }
    // Moving a native cutter updates the exact intersection of its trimmed offset.
    for(bool flipped:{false,true}) {
        auto s=Sketch::create_default();const auto baseline=s.add_segment(0,0,20,0),offset=s.add_offset(baseline,2,flipped);
        const auto cutter=s.add_segment(10,-5,10,5);const auto line=s.segments.back();
        static_cast<void>(s.retain_curve_intervals(offset,{{0,.5}}));
        const auto anchor=s.find_offset(offset)->end_anchor;check(anchor.has_value(),"Missing offset trim anchor");
        for(double delta:{.2,-.4,.3}) {
            const auto before=s;const auto p=grip(s,line,.7);check(s.drag_contour(cutter,p,{p[0]+delta,p[1]}),"Trim cutter drag failed");++gestures;
            const auto end=kernel::bspline_value(sketch_curve_geometry(s,offset),1);
            near(end.x,p[0]+delta,"Trimmed offset failed to follow cutter");near(end.y,flipped?-2:2,"Trimmed offset side changed");
            const auto updated=s.find_offset(offset)->end_anchor;
            check(!s.find_offset(offset)->broken&&updated&&updated->curve_id==anchor->curve_id,"Trim anchor identity changed");
            near(updated->parameter,anchor->parameter,"Trim anchor changed intersection branch");contracts(before,s);
        }
        ++cases;
    }
    for(int state:{0,1,2})for(bool reverse:{false,true}) {
        auto s=Sketch::create_default();const auto id=s.add_circle(0,10,10),center=s.circles.front().center_point_id,partner=s.add_point(0,-10);
        static_cast<void>(s.add_tangent_constraint(id,"sketch_axis:x"));
        auto d=s.create_symmetric_dimension(reverse?partner:center,reverse?center:partner,"sketch_axis:x");d.driving=state!=0;d.locked=state==2;s.apply_dimension(d);
        for(double radius:{11.,9.,12.}) {
            const auto before=s;const auto p=point(s,center);const auto r=s.circles.front().radius;
            const bool moved=s.drag_contour(id,{p[0]+r,p[1]},{p[0]+radius,p[1]});++gestures;
            check(moved==(state!=2),"Symmetric circle mobility state="+std::to_string(state)+" reverse="+std::to_string(reverse)+" radius="+std::to_string(radius)+" moved="+std::to_string(moved));
            if(state==2)check(s.serialized()==before.serialized(),"Rejected symmetric circle changed");
            else {near(s.circles.front().radius,radius,"Symmetric circle radius");near(s.find_point(center)->y,radius,"Circle lost tangent side");
                if(state==1){near(s.find_point(partner)->y,-radius,"Symmetric partner did not follow");near(s.find_point(partner)->x,s.find_point(center)->x,"Symmetric partner alignment");}}
            contracts(before,s);
        }
        ++cases;
    }
}
void structural_dimensions() {
    for(bool external:{false,true})for(double u:{.1,.5,.9}) {
        auto s=Sketch::create_default();const auto id=s.add_segment(-10,0,10,0),pin=s.add_point(0,0);const auto line=s.segments.front();
        if(external){auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);ref.source_document_id="ground";ref.source_owner_id="pin";ref.source_semantic_key="midpoint";ref.cached_points={{0,0}};s.add_external_reference(ref);static_cast<void>(s.add_point_reference_constraint(pin,ref.id));}
        else s.find_point(pin)->fixed=true;
        static_cast<void>(s.add_midpoint_constraint(pin,id));
        for(double angle:{.2,-.3,.4}) {
            const auto before=s;const auto p=grip(s,line,u);const double arm=20*u-10;
            const bool moved=s.drag_contour(id,p,{arm*std::cos(angle),arm*std::sin(angle)});++gestures;
            check(moved==(u!=.5),"Anchored midpoint lever mobility");
            if(u!=.5){const auto a=point(s,line.first_point_id);near(a[0],-10*std::cos(angle),"Midpoint lever X");near(a[1],-10*std::sin(angle),"Midpoint lever Y");}
            near(length(s,line),20,"Midpoint lever length");contracts(before,s);
        }
        ++cases;
    }
    for(bool reverse:{false,true})for(int state:{0,1,2}) {
        auto s=Sketch::create_default();const auto id=reverse?s.add_segment(10,5,-10,5):s.add_segment(-10,5,10,5);const auto line=s.segments.front();
        auto d=s.create_symmetric_dimension(line.first_point_id,line.second_point_id,"sketch_axis:y");d.driving=state!=0;d.locked=state==2;s.apply_dimension(d);
        for(double delta:{2.,-3.,1.}) {
            const auto before=s;const auto p=grip(s,line,.5);check(s.drag_contour(id,p,{p[0]+1,p[1]+delta}),"Symmetric segment did not slide state="+std::to_string(state)+" reverse="+std::to_string(reverse));++gestures;
            const auto a=point(s,line.first_point_id),b=point(s,line.second_point_id);near(length(s,line),20,"Symmetric line length");
            if(state){near(a[0]+b[0],0,"Symmetric line escaped axis");near(a[1],b[1],"Freely translating symmetric line rotated");}
            near((a[1]+b[1])/2,p[1]+delta,"Symmetric slide missed cursor");contracts(before,s);
        }
        ++cases;
    }
    for(unsigned kind=0;kind<5;++kind)for(bool reverse:{false,true})for(int state:{0,1,2}) {
        auto s=Sketch::create_default();const auto id=reverse?s.add_segment(10,10,0,0):s.add_segment(0,0,10,10);const auto line=s.segments.front();
        const auto pivot=reverse?line.second_point_id:line.first_point_id,end=reverse?line.first_point_id:line.second_point_id;
        s.find_point(pivot)->fixed=true;const auto support=s.add_segment(0,0,20,0),ref=s.segments.back().second_point_id;s.find_point(ref)->fixed=true;
        SketchDimension d;
        if(kind==0)d=s.create_line_pair_dimension(reverse?id:support,reverse?support:id,DimensionKind::AngleBetween);
        if(kind==1)d=s.create_point_line_angle_dimension(reverse?end:pivot,reverse?pivot:end,"sketch_axis:x");
        if(kind==2)d=s.create_three_point_angle_dimension(reverse?end:ref,pivot,reverse?ref:end);
        if(kind==3)d=s.create_four_point_angle_dimension(pivot,ref,reverse?end:pivot,reverse?pivot:end);
        if(kind==4)d=s.create_line_symmetric_dimension("sketch_axis:x",id,{},DimensionKind::AngleSymmetric);
        d.driving=state!=0;d.locked=state==2;s.apply_dimension(d);
        for(double angle:{.8,.65,.75}) {
            const auto before=s;const auto p=grip(s,line,.7);const double fraction=reverse?.3:.7,r=std::sqrt(200.);
            const bool moved=s.drag_contour(id,p,{fraction*r*std::cos(angle),fraction*r*std::sin(angle)});++gestures;
            check(moved==(state!=2),"Angular contour mobility kind="+std::to_string(kind));
            if(state==2)check(s.serialized()==before.serialized(),"Locked angular contour changed");
            else {const auto q=point(s,end);near(q[0],r*std::cos(angle),"Angular contour X");near(q[1],r*std::sin(angle),"Angular contour Y");}
            contracts(before,s);
        }
        ++cases;
    }
}
void benchmark() {
    for(unsigned count:{1u,200u}) {
        auto source=Sketch::create_default();const auto line=source.add_segment(0,0,20,0);
        for(unsigned n=1;n<count;++n)static_cast<void>(source.add_circle(100+3*n,100,1));
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<100;++i){auto s=source;check(s.drag_contour(line,{10,0},{11,2}),"Benchmark gesture failed");near(s.find_point(s.segments.front().first_point_id)->x,1,"Benchmark output");}
        std::cout<<"Contour translation synthetic "<<count<<" curves: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100<<" ms/gesture (including caller copy)\n";
    }
}
}
int main() {
    try {
        const auto start=std::chrono::steady_clock::now();lines();constrained_lines();circles();circle_relations();mechanisms();circular_contacts();dependencies();structural_dimensions();benchmark();
        std::cout<<"Contour matrix: "<<cases<<" cases, "<<gestures<<" gestures, "<<point_drags<<" point drags, "<<edits<<" dimension edits, "<<checks<<" checks, "
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    } catch(const std::exception& e) {std::cerr<<"Contour regression: "<<e.what()<<'\n';return 1;}
}
