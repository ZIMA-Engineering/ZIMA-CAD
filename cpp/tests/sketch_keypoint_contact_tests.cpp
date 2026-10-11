#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace zima::sketcher;
namespace {
std::string matrix_case;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Fixture {
    Sketch sketch=Sketch::create_default();
    std::string curve,center,point,segment,key,k,c;
    int kind{},quarter{};
    Fixture(int type,int q,double rotation,bool reverse,bool existing_c,bool free_support):kind(type),quarter(q) {
        const double co=std::cos(rotation),si=std::sin(rotation),hand=reverse?-1.:1.;
        const auto xy=[&](double t){return std::array{8*co*std::cos(t)-4*hand*si*std::sin(t),8*si*std::cos(t)+4*hand*co*std::sin(t)};};
        const double start=.15,end=6.13;
        if(type==0)curve=sketch.add_circle(0,0,8);
        if(type==1)curve=sketch.add_arc(0,0,8*std::cos(start),8*std::sin(start),8*std::cos(end),8*std::sin(end));
        if(type==2)curve=sketch.add_ellipse(0,0,8*co,8*si,-4*hand*si,4*hand*co);
        if(type==3){const auto a=xy(start),b=xy(end);curve=sketch.add_elliptical_arc(0,0,8*co,8*si,-4*hand*si,4*hand*co,a[0],a[1],b[0],b[1],reverse);}
        center=type==0?sketch.circles.front().center_point_id:type==1?sketch.arcs.front().center_point_id:
            type==2?sketch.ellipses.front().center_point_id:sketch.elliptical_arcs.front().center_point_id;
        if(!free_support) {
            for(auto& p:sketch.points)p.fixed=true;
            if(type<2){auto dim=type==0?sketch.create_circle_radius_dimension(curve):sketch.create_arc_radius_dimension(curve);dim.locked=true;sketch.apply_dimension(dim);}
        }
        const double t=q*std::numbers::pi/2;
        const auto target=type<2?std::array{8*std::cos(t),8*std::sin(t)}:xy(t);
        segment=sketch.add_segment(target[0]+.2,target[1]+.3,target[0]+3,target[1]+5,0,false,false,false);
        point=sketch.segments.back().first_point_id;
        const char* names[]{"circle","arc","ellipse","elliptical_arc"};
        key="sketch_keypoint:"+std::string(names[type])+":"+curve+":"+std::to_string(q);
        if(existing_c)c=sketch.add_point_on_circle_constraint(point,curve);
        k=sketch.add_point_reference_constraint(point,key);
        const auto found=std::ranges::find_if(sketch.constraints,[&](const auto& v){return v.kind==ConstraintKind::PointOnCircle&&v.first_point_id==point&&v.geometry_id==curve&&!v.suppressed;});
        check(found!=sketch.constraints.end(),"Characteristic reference has no C support");
        if(!c.empty())check(found->id==c,"Existing C identity changed");c=found->id;
        check(std::ranges::count_if(sketch.constraints,[&](const auto& v){return v.kind==ConstraintKind::PointOnCircle&&v.first_point_id==point&&v.geometry_id==curve&&!v.suppressed;})==1,"Duplicate C contact");
    }
    void equations(bool exact) {
        const auto result=sketch.solve();check(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-6,"C+K failed to solve");
        const auto* p=sketch.find_point(point);const auto* o=sketch.find_point(center);double a{},b{},r{};
        if(kind<2){a=b=kind==0?sketch.circles.front().radius:sketch.arcs.front().radius;}
        else if(kind==2){const auto& e=sketch.ellipses.front();a=e.major_radius;b=e.minor_radius;r=e.rotation;}
        else {const auto& e=sketch.elliptical_arcs.front();a=e.major_radius;b=e.minor_radius;r=e.rotation;}
        const double x=(p->x-o->x)*std::cos(r)+(p->y-o->y)*std::sin(r),y=-(p->x-o->x)*std::sin(r)+(p->y-o->y)*std::cos(r);
        check(std::abs(x*x/(a*a)+y*y/(b*b)-1)<1e-6,"Point escaped independent conic equation");
        if(exact){
            const double t=quarter*std::numbers::pi/2;
            double tx=o->x+a*std::cos(t),ty=o->y+b*std::sin(t);
            if(kind>=2){const auto major=kind==2?sketch.ellipses.front().major_point_id:sketch.elliptical_arcs.front().major_point_id;
                const auto minor=kind==2?sketch.ellipses.front().minor_point_id:sketch.elliptical_arcs.front().minor_point_id;
                const auto* m=sketch.find_point(major);const auto* n=sketch.find_point(minor);
                tx=o->x+(m->x-o->x)*std::cos(t)+(n->x-o->x)*std::sin(t);
                ty=o->y+(m->y-o->y)*std::cos(t)+(n->y-o->y)*std::sin(t);
            }
            check(std::hypot(p->x-tx,p->y-ty)<1e-6,"K escaped its independent characteristic position");
        }
    }
};
}
int main(){try {
    unsigned cases{},existing_limitations{};
    for(int kind=0;kind<4;++kind)for(int q=0;q<4;++q) {
        if((kind==1||kind==3)&&q==0)continue; // Outside the intentionally open arc interval.
        for(bool reverse:{false,true})for(bool existing:{false,true})for(bool free:{false,true})for(double rotation:{0.,.37,std::numbers::pi/2}) {
            Fixture f(kind,q,kind>=2?rotation:0,reverse,existing,free);f.equations(true);
            const auto before=f.sketch.serialized();bool rejected=false;
            try{static_cast<void>(f.sketch.add_point_reference_constraint(f.point,f.key));}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Duplicate K changed the document");
            for(int i=0;i<3;++i) {
                const auto* p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.3,p->y-.2));f.equations(true);
                const auto endpoint=f.sketch.segments.back().second_point_id;const auto* e=f.sketch.find_point(endpoint);
                check(f.sketch.move_point(endpoint,e->x+.4,e->y+.2),"Unconstrained endpoint did not move");f.equations(true);
            }
            f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(true);
            const auto with_k=f.sketch;const auto rank_before=f.sketch.solve().remaining_degrees_of_freedom;
            f.sketch.remove_constraint(f.k);f.equations(false);
            check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==f.c&&!c.suppressed;}),"Removing K removed C identity");
            const auto rank_after=f.sketch.solve().remaining_degrees_of_freedom;
            if(rank_after!=rank_before+1)throw std::runtime_error("Removing K rank mismatch: kind="+std::to_string(kind)+" quarter="+std::to_string(q)+" free="+std::to_string(free)+" before="+std::to_string(rank_before)+" after="+std::to_string(rank_after));
            const auto source_points=f.sketch.points;const auto* p=f.sketch.find_point(f.point);const auto start=std::array{p->x,p->y};
            for(int i=0;i<3;++i){p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.5,p->y+.6));f.equations(false);}
            p=f.sketch.find_point(f.point);check(std::hypot(start[0]-p->x,start[1]-p->y)>1e-4,"C-only point did not slide");
            if(!free)for(const auto& source:source_points)if(source.fixed)check(*f.sketch.find_point(source.id)==source,"Sliding contact changed fixed support");
            f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(false);
            f.sketch=with_k;f.sketch.remove_constraint(f.c);f.equations(true); // C and K are independently removable.
            ++cases;
        }
    }
    // Driving/reference, radius/diameter and ellipse axis/rotation dimensions,
    // locked/unlocked editing and repeated value changes of the support.
    for(int kind=0;kind<3;++kind)for(bool locked:{false,true})for(bool reference:{false,true})for(int dim_kind=0;dim_kind<(kind<2?2:3);++dim_kind) {
        Fixture f(kind,1,.37,false,false,true);f.sketch.set_point_fixed(f.center,true);
        auto dim=kind==0?(dim_kind?f.sketch.create_circle_diameter_dimension(f.curve):f.sketch.create_circle_radius_dimension(f.curve)):
            kind==1?(dim_kind?f.sketch.create_arc_diameter_dimension(f.curve):f.sketch.create_arc_radius_dimension(f.curve)):
            dim_kind==2?f.sketch.create_ellipse_rotation_dimension(f.curve):f.sketch.create_ellipse_radius_dimension(f.curve,dim_kind==0);
        dim.locked=locked;dim.driving=!reference;
        if(locked&&reference){const auto before=f.sketch.serialized();bool rejected=false;try{f.sketch.apply_dimension(dim);}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Locked reference dimension changed the document");dim.locked=false;}
        const auto id=dim.id;f.sketch.apply_dimension(dim);f.equations(true);
        if(!reference)for(double multiplier:{1.1,.9,1.05}){check(f.sketch.set_dimension_value(id,dim.value*multiplier),"C+K rejected support dimension edit");f.equations(true);}
        const auto contact=f.c;f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){const auto* p=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,p->x+.4,p->y+.2));f.equations(false);}
        check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==contact;}),"Dimension edit changed C reference identity");++cases;
    }
    for(int kind=0;kind<4;++kind)for(bool locked:{false,true})for(bool reference:{false,true})for(auto dimension_kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle}) {
        Fixture f(kind,2,.37,false,false,false);
        auto dimension=f.sketch.create_segment_dimension(f.segment,dimension_kind);dimension.locked=locked;dimension.driving=!reference;
        if(locked&&reference){const auto before=f.sketch.serialized();bool rejected=false;try{f.sketch.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Locked reference segment dimension changed the document");dimension.locked=false;}
        const auto id=dimension.id;f.sketch.apply_dimension(dimension);f.equations(true);
        if(!reference)for(double delta:{.2,-.1,.3}) {check(f.sketch.set_dimension_value(id,dimension.value+delta),"C+K rejected segment dimension edit");f.equations(true);}
        const auto endpoint=f.sketch.segments.back().second_point_id;
        for(int i=0;i<3;++i){const auto* end=f.sketch.find_point(endpoint);static_cast<void>(f.sketch.move_point(endpoint,end->x+.3,end->y+.2));f.equations(true);}
        f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){const auto* contact=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,contact->x+.3,contact->y+.2));f.equations(false);}
        ++cases;
    }
    for(int kind=0;kind<4;++kind) {
    matrix_case="fully constrained support="+std::to_string(kind);
        Fixture f(kind,2,.37,false,false,false);
        auto length=f.sketch.create_segment_dimension(f.segment,DimensionKind::Distance);length.locked=true;f.sketch.apply_dimension(length);
        auto angle=f.sketch.create_segment_dimension(f.segment,DimensionKind::Angle);angle.locked=true;f.sketch.apply_dimension(angle);
        check(f.sketch.solve().remaining_degrees_of_freedom==0,"Locked segment on fixed support is not fully constrained");
        const auto before=f.sketch.serialized();const auto* end=f.sketch.find_point(f.sketch.segments.back().second_point_id);
        static_cast<void>(f.sketch.move_point(end->id,end->x+2,end->y+2));f.equations(true);
        check(f.sketch.serialized()==before,"Fully constrained C+K segment escaped by dragging");
        auto impossible=f.sketch.create_segment_dimension(f.segment,DimensionKind::Distance);impossible.value+=2;bool rejected=false;
        try{f.sketch.apply_dimension(impossible);}catch(const std::exception&){rejected=true;}
        check(rejected&&f.sketch.serialized()==before,"Impossible dimension changed C+K document");++cases;
    }
    for(int kind=0;kind<4;++kind)for(int dimension_kind=0;dimension_kind<14;++dimension_kind)
        for(bool order:{false,true})for(bool reference:{false,true})for(bool locked:{false,true}) {
        Fixture f(kind,dimension_kind==10?1:2,dimension_kind==10?0:.37,false,false,dimension_kind!=10);const auto end=f.sketch.segments.back().second_point_id;
        if(dimension_kind==10){const auto* p=f.sketch.find_point(f.point);check(f.sketch.move_point(end,p->x,-p->y),"Symmetric fixture could not prepare opposite contact");}
        const auto first=order?end:f.point,second=order?f.point:end;
        matrix_case="support="+std::to_string(kind)+" dimension="+std::to_string(dimension_kind)+" order="+std::to_string(order)+" reference="+std::to_string(reference)+" locked="+std::to_string(locked);
        if(dimension_kind==3){const auto before=f.sketch.serialized();bool rejected=false;
            try{static_cast<void>(f.sketch.create_point_dimension(first,second,DimensionKind::Angle));}catch(const std::invalid_argument&){rejected=true;}
            check(rejected&&f.sketch.serialized()==before,"Unsupported two-point angle changed the document");continue;}
        const auto reference_point=f.sketch.add_point(16,3);f.sketch.set_point_fixed(reference_point,true);
        SketchDimension d;
        if(dimension_kind<4){const DimensionKind kinds[]{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle};d=f.sketch.create_point_dimension(first,second,kinds[dimension_kind]);}
        if(dimension_kind==4)d=f.sketch.create_axis_dimension(end,"sketch_axis:x");
        if(dimension_kind==5)d=f.sketch.create_axis_dimension(end,"sketch_axis:y");
        if(dimension_kind==6)d=f.sketch.create_point_line_dimension(end,"sketch_axis:x");
        if(dimension_kind==7)d=f.sketch.create_point_line_angle_dimension(first,second,"sketch_axis:x");
        if(dimension_kind==8)d=f.sketch.create_three_point_angle_dimension(first,f.center,second);
        if(dimension_kind==9)d=f.sketch.create_four_point_angle_dimension(f.center,reference_point,first,second);
        if(dimension_kind==10)d=f.sketch.create_symmetric_dimension(first,second,"sketch_axis:x");
        if(dimension_kind==11){static_cast<void>(f.sketch.add_segment_constraint(f.segment,ConstraintKind::Horizontal));d=f.sketch.create_line_pair_dimension("sketch_axis:x",f.segment,DimensionKind::DistanceLine);}
        if(dimension_kind==12)d=f.sketch.create_line_symmetric_dimension("sketch_axis:x",f.segment,{},DimensionKind::AngleSymmetric);
        if(dimension_kind==13){static_cast<void>(f.sketch.add_segment_constraint(f.segment,ConstraintKind::Horizontal));d=f.sketch.create_line_symmetric_dimension("sketch_axis:x",f.segment,{},DimensionKind::DistanceLineSymmetric);}
        d.driving=!reference;d.locked=locked&&!reference;const auto id=d.id;f.sketch.apply_dimension(d);f.equations(true);
        const auto verify_dimension=[&] {
            const auto& actual=*std::ranges::find(f.sketch.dimensions,id,&SketchDimension::id);
            const auto position=[&](const std::string& key){const auto* p=f.sketch.find_point(key);check(p,"Independent dimension point missing");return std::array{p->x,p->y};};
            const auto a=position(first),b=position(second),o=position(f.center);
            const auto angle=[](std::array<double,2> u,std::array<double,2> v){return std::acos(std::clamp((u[0]*v[0]+u[1]*v[1])/(std::hypot(u[0],u[1])*std::hypot(v[0],v[1])),-1.,1.))*180/std::numbers::pi;};
            const std::array direction{b[0]-a[0],b[1]-a[1]};double measured{};
            if(dimension_kind==0)measured=std::hypot(direction[0],direction[1]);
            if(dimension_kind==1)measured=direction[0];
            if(dimension_kind==2)measured=direction[1];
            if(dimension_kind==4)measured=position(end)[1];
            if(dimension_kind==5)measured=position(end)[0];
            if(dimension_kind==6)measured=std::abs(position(end)[1]);
            if(dimension_kind==7)measured=angle({1,0},direction);
            if(dimension_kind==8)measured=angle({a[0]-o[0],a[1]-o[1]},{b[0]-o[0],b[1]-o[1]});
            if(dimension_kind==9){const auto r=position(reference_point);measured=angle({r[0]-o[0],r[1]-o[1]},direction);}
            if(dimension_kind==10){measured=std::abs(a[1])+std::abs(b[1]);if(actual.driving)check(std::abs(std::abs(a[1])-std::abs(b[1]))<1e-6,"Independent symmetric point equation failed");}
            if(dimension_kind>=11){const auto segment=std::ranges::find(f.sketch.segments,f.segment,&SketchSegment::id);const auto p=position(segment->first_point_id),q=position(segment->second_point_id);
                if(dimension_kind==12){const double theta=angle({1,0},{q[0]-p[0],q[1]-p[1]});measured=2*std::min(theta,180-theta);}
                else {measured=std::abs(p[1])*(dimension_kind==13?2:1);if(actual.driving)check(std::abs(p[1]-q[1])<1e-6,"Independent parallel line equation failed");}}
            check(std::abs(std::abs(measured)-std::abs(actual.value))<1e-6,"Independent dimension equation failed");
        };verify_dimension();
        if(!reference)for(double delta:{.2,-.1,.3}){
            matrix_case+=" value="+std::to_string(d.value+delta);
            const auto before=f.sketch.serialized();
            if(!f.sketch.set_dimension_value(id,d.value+delta)) {
                check(dimension_kind==10||dimension_kind==11||dimension_kind==13,"C+K rejected point/contact dimension edit");
                check(f.sketch.serialized()==before,"Rejected contact dimension changed the document");
                auto k_only=f.sketch;k_only.remove_constraint(f.c);const auto baseline=k_only.serialized();
                check(!k_only.set_dimension_value(id,d.value+delta)&&k_only.serialized()==baseline,"C+K introduced a dimension rejection absent from K-only support");
                ++existing_limitations;
            }
            f.equations(true);
            verify_dimension();
        }
        for(int i=0;i<3;++i){matrix_case+=" endpoint-drag";const auto* endpoint=f.sketch.find_point(end);static_cast<void>(f.sketch.move_point(end,endpoint->x+.2,endpoint->y+.3));f.equations(true);verify_dimension();}
        f.sketch.remove_constraint(f.k);f.equations(false);
        for(int i=0;i<3;++i){matrix_case+=" contact-drag";const auto* endpoint=f.sketch.find_point(f.point);static_cast<void>(f.sketch.move_point(f.point,endpoint->x+.2,endpoint->y+.3));f.equations(false);verify_dimension();}
        f.sketch=Sketch::from_serialized(f.sketch.serialized());f.equations(false);verify_dimension();++cases;
    }
    for(int kind=0;kind<4;++kind)for(bool remove_k:{false,true}) {
        Fixture f(kind,2,0,false,false,true);if(remove_k)f.sketch.remove_constraint(f.k);
        const auto pieces=sketch_trim_topology(f.sketch);std::optional<SketchTrimPiece> removed;
        for(const auto& piece:pieces)if(piece.geometry_id==f.curve&&!piece.points.empty()&&piece.points[piece.points.size()/2][0]>0) {removed=piece;break;}
        check(removed.has_value(),"Trim contact fixture has no opposite curve piece");
        const auto mapping=apply_sketch_trim(f.sketch,{*removed});
        check(mapping.geometry_mapping.contains(f.curve),"Trim did not remap the supporting curve");
        const auto contact=std::ranges::find_if(f.sketch.constraints,[&](const auto& c){return c.id==f.c;});
        check(contact!=f.sketch.constraints.end()&&contact->first_point_id==f.point&&contact->kind==ConstraintKind::PointOnCircle,"Trim lost independent C identity");
        check(std::ranges::count_if(f.sketch.constraints,[&](const auto& c){return c.kind==ConstraintKind::PointOnCircle&&c.first_point_id==f.point;})==1,"Trim converted K into a duplicate C");
        const auto result=f.sketch.solve();check(result.status!=SolveStatus::Invalid&&result.status!=SolveStatus::Conflicting&&result.maximum_residual<1e-6,"Trimmed C+K contact does not solve");
        const auto* point=f.sketch.find_point(f.point);check(point,"Trim lost the attached native point");
        f.sketch=Sketch::from_serialized(f.sketch.serialized());check(std::ranges::any_of(f.sketch.constraints,[&](const auto& c){return c.id==f.c;}),"Trim persistence lost C identity");++cases;
    }
    // Projected external conics are native analytic supports but remain read-only.
    for(int kind=0;kind<4;++kind) {
    matrix_case="external support="+std::to_string(kind);
    auto external=Sketch::create_default();auto source=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    source.source_document_id="external-document";source.source_owner_id="external-curve";source.source_semantic_key="conic";
    const double b=kind<2?8:4,start=(kind==1||kind==3)?.15:0,end=(kind==1||kind==3)?6.13:2*std::numbers::pi;
    for(int i=0;i<=32;++i){const double t=start+(end-start)*i/32;source.cached_points.push_back({8*std::cos(t),b*std::sin(t)});}
    external.add_external_reference(source);const auto support=external.add_external_profile_geometry(source.id);
    const auto source_points=external.points;const auto contact=external.add_point(-7,1);
    const char* names[]{"circle","arc","ellipse","elliptical_arc"};
    if(kind==1||kind==3){
        check(std::ranges::any_of(external.bsplines,[&](const auto& curve){return curve.id==support;}),"Open external conic profile changed representation");
        const auto before=external.serialized();bool rejected=false;
        try{static_cast<void>(external.add_point_reference_constraint(contact,"sketch_keypoint:"+std::string(names[kind])+":"+support+":2"));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&external.serialized()==before,"Nonanalytic external profile accepted an invalid conic K");++cases;continue;
    }
    const auto anchor=external.add_point_reference_constraint(contact,"sketch_keypoint:"+std::string(names[kind])+":"+support+":2");
    external.remove_constraint(anchor);
    for(int i=0;i<3;++i){const auto* current=external.find_point(contact);check(external.move_point(contact,current->x+.4,current->y+.6),"External C-only contact did not move");
        current=external.find_point(contact);check(std::abs(current->x*current->x/64+current->y*current->y/(b*b)-1)<1e-6,"External C-only contact escaped conic");
        for(const auto& p:source_points)check(*external.find_point(p.id)==p,"Contact drag modified read-only projected source");
        check(external.external_references.front()==source,"Contact drag rewrote external reference packet");}
    ++cases;
    }
    auto origin=Sketch::create_default();const auto p=origin.add_point(1,2);static_cast<void>(origin.add_point_reference_constraint(p,"sketch_origin"));
    check(origin.constraints.size()==1&&origin.constraints.front().kind==ConstraintKind::PointReference,"Origin K acquired a curve contact");
    std::cout<<"C+K native matrix: "<<cases<<" cases, independent conic equations, successive drags and value edits, fixed/free support, C reuse, independent removal and persistence passed; "<<existing_limitations<<" existing K-only dimension edit limitations rejected atomically\n";return 0;
}catch(const std::exception& e){std::cerr<<matrix_case<<": "<<e.what()<<'\n';return 1;}}
