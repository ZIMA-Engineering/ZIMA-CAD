#include <zima/sketcher/curve_fillet.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace zima::sketcher;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
double distance(zima::kernel::Vec3 a,zima::kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Fixture {Sketch sketch;std::string a,b;};
Fixture fixture(unsigned type,int orientation,unsigned first_type=0,bool mirror=false) {
    Fixture f{Sketch::create_default()};auto& s=f.sketch;
    if(first_type==0)f.a=s.add_segment(0,0,25,0);
    if(first_type==1)f.a=s.add_arc(0,-10,10,-10,0,0);
    if(first_type==2)f.a=s.add_elliptical_arc(0,-15,15,-15,0,0,15,-15,0,0);
    if(first_type>=3)f.a=s.add_bspline({{0,0},{7,0},{14,-5},{20,-12}},3,false,false,1e-6,first_type==4);
    if(type==0)f.b=s.add_segment(0,0,0,20);
    if(type==1)f.b=s.add_arc(-10,0,0,0,-10,10);
    if(type==2)f.b=s.add_elliptical_arc(-15,0,0,0,-15,10,0,0,-15,10);
    if(type>=3)f.b=s.add_bspline({{0,0},{0,7},{5,14},{12,20}},3,false,false,1e-6,type==4);
    // A rigid rotation exercises both material/contact sides without changing
    // the support parameterization or substituting tessellated curves.
    const double angle=orientation*1.5707963267948966;
    for(auto& p:s.points){const auto x=p.x,y=p.y;p.x=x*std::cos(angle)-y*std::sin(angle);p.y=x*std::sin(angle)+y*std::cos(angle);}
    for(auto& c:s.arcs){c.start_angle+=angle;c.end_angle+=angle;}
    for(auto& c:s.elliptical_arcs)c.rotation+=angle;
    if(mirror) {
        for(auto& p:s.points)p.x=-p.x;
        for(auto& c:s.arcs) {
            const double start=c.start_angle;c.start_angle=std::acos(-1.)-c.end_angle;c.end_angle=std::acos(-1.)-start;
            std::swap(c.start_point_id,c.end_point_id);
        }
        for(auto& c:s.elliptical_arcs){c.rotation=std::acos(-1.)-c.rotation;c.reversed=!c.reversed;}
    }
    s.constraints.clear();s.validate();return f;
}
void equations(const Sketch& s,const std::string& arc_id,const std::string& a,const std::string& b,double expected) {
    const auto arc=std::ranges::find(s.arcs,arc_id,&SketchArc::id);
    check(arc!=s.arcs.end(),"Missing fillet arc");
    check(std::abs(arc->radius-expected)<1e-6,"Fillet radius changed");
    const auto* cp=s.find_point(arc->center_point_id);const zima::kernel::Vec3 center{cp->x,cp->y,0};
    for(const auto& id:{a,b}) {
        const auto ends=fillet_curve_endpoints(s,id);check(bool(ends),"Trimmed support missing");
        const auto curve=sketch_curve_geometry(s,id);
        bool joined{};
        for(int end=0;end<2;++end)if((*ends)[end]==arc->start_point_id||(*ends)[end]==arc->end_point_id) {
            joined=true;const auto p=zima::kernel::bspline_value(curve,double(end));
            const auto q=zima::kernel::bspline_value(curve,end?1.-1e-7:1e-7);
            const auto dx=q.x-p.x,dy=q.y-p.y;
            check(std::abs(distance(p,center)-expected)<1e-6,"Contact left the tangent circle");
            check(std::abs((p.x-center.x)*dx+(p.y-center.y)*dy)/std::hypot(dx,dy)/expected<1e-5,"Fillet tangent equation failed");
            check(std::ranges::any_of(s.constraints,[&](const auto& c){return c.kind==ConstraintKind::Tangent&&
                c.first_point_id==(*ends)[end]&&c.geometry_id==id&&c.second_geometry_id==arc_id;}),"Missing point-based tangency");
        }
        check(joined,"Fillet created duplicate coincident endpoints");
    }
}
}
int main(){try {
    const auto start=std::chrono::steady_clock::now();unsigned cases{};
    for(unsigned first_type=0;first_type<5;++first_type)for(unsigned type=0;type<5;++type)for(int orientation=0;orientation<4;++orientation)for(bool reverse:{false,true})for(bool mirror:{false,true}) {
        auto f=fixture(type,orientation,first_type,mirror);if(reverse)std::swap(f.a,f.b);
        const auto original_points=f.sketch.points;
        std::string stable;
        for(double radius:{.5,1.,.75}) {
            auto& s=f.sketch;
            const auto id=s.add_corner_fillet(f.a,f.b,radius).arc_id;
            check(stable.empty()||stable==id,"Virtual radius identity changed");stable=id;
            check(s.points==original_points,"Virtual radius moved authored points");
            equations(s.evaluated_profile_sketch(),id,f.a,f.b,radius);
            const auto reopened=Sketch::from_serialized(s.serialized());
            equations(reopened.evaluated_profile_sketch(),id,f.a,f.b,radius);
        }
        auto real=fixture(type,orientation,first_type,mirror);if(reverse)std::swap(real.a,real.b);
        const auto added=fillet_sketch_curves(real.sketch,real.a,real.b,1.);
        check(real.sketch.dimensions.empty()&&real.sketch.corner_radii.empty(),"Real fillet inserted an automatic dimension or virtual radius");
        equations(real.sketch,added.arc_id,real.a,real.b,1.);
        equations(Sketch::from_serialized(real.sketch.serialized()),added.arc_id,real.a,real.b,1.);
        {
            auto released=real.sketch;
            std::vector<std::string> tangencies;
            for(const auto& c:released.constraints)if(c.kind==ConstraintKind::Tangent&&c.second_geometry_id==added.arc_id)tangencies.push_back(c.id);
            for(const auto& id:tangencies)released.remove_constraint(id);
            const auto ends_a=fillet_curve_endpoints(released,real.a),ends_b=fillet_curve_endpoints(released,real.b);
            check(ends_a&&ends_b&&released.find_point(added.first_tangent_point_id)&&released.find_point(added.second_tangent_point_id),"Removing T deleted native contacts");
            check(std::ranges::find(*ends_a,added.first_tangent_point_id)!=ends_a->end()&&std::ranges::find(*ends_b,added.second_tangent_point_id)!=ends_b->end(),"Removing T detached support endpoints");
            check(released.constraints.empty(),"Fillet introduced redundant C at shared native contacts");
        }
        for (auto kind : {DimensionKind::Radius,DimensionKind::Diameter}) for (bool locked : {false,true}) {
            auto edited=real.sketch;
            SketchDimension d;d.id="fillet-size";d.geometry_id=added.arc_id;d.kind=kind;
            const double scale=kind==DimensionKind::Diameter?2.:1.;d.value=scale;d.locked=locked;
            edited.dimensions.push_back(d);
            check(edited.solve().status!=SolveStatus::Conflicting,"Initial fillet dimension conflicted");
            for(double radius:{1.2,.8,1.1}) {
                check(edited.set_dimension_value(d.id,radius*scale),"Fillet radius dimension edit rejected");
                equations(edited,added.arc_id,real.a,real.b,radius);
                const auto arc=std::ranges::find(edited.arcs,added.arc_id,&SketchArc::id);
                const auto center_id=arc->center_point_id;
                const auto before=*edited.find_point(center_id);
                check(edited.move_point(center_id,before.x+1.,before.y+.5),"Free fillet center could not move");
                const auto after=*edited.find_point(center_id);
                check(std::hypot(after.x-before.x,after.y-before.y)>.01,"Permitted fillet drag did not move");
                equations(edited,added.arc_id,real.a,real.b,radius);
                edited=Sketch::from_serialized(edited.serialized());
            }
            auto reference=edited;reference.dimensions.front().driving=false;reference.dimensions.front().locked=false;
            static_cast<void>(reference.set_dimension_value(d.id,2.));
            check(reference.points==edited.points && reference.arcs==edited.arcs && !reference.dimensions.front().driving &&
                std::abs(reference.dimensions.front().value-*reference.circular_radius(added.arc_id)*scale)<1e-7,
                "Reference radius drove geometry or stopped measuring");
            for(auto& point:edited.points)point.fixed=true;
            const auto fixed_before=edited.serialized();const auto contact=added.first_tangent_point_id;
            const auto* point=edited.find_point(contact);
            check(!edited.move_point(contact,point->x+2,point->y+1)&&edited.serialized()==fixed_before,"Fixed fillet escaped its constraints");
        }
        {
            auto placed=real.sketch;
            const auto& arc=*std::ranges::find(placed.arcs,added.arc_id,&SketchArc::id);
            const auto center_id=arc.center_point_id;
            SketchDimension radius;radius.id="locked-radius";radius.geometry_id=added.arc_id;
            radius.kind=DimensionKind::Radius;radius.value=1.;radius.locked=true;placed.dimensions.push_back(radius);
            for(auto kind:{DimensionKind::DistanceX,DimensionKind::DistanceY}) {
                auto d=placed.create_point_dimension("sketch_origin",center_id,kind);d.locked=true;
                placed.apply_dimension(d);
                for(double shift:{.25,-.4,.1}) {
                    const auto center=*placed.find_point(center_id);const auto value=(kind==DimensionKind::DistanceX?center.x:center.y)+shift;
                    check(placed.set_dimension_value(d.id,value),"Fillet center coordinate edit rejected");
                    const auto* updated=placed.find_point(center_id);
                    check(std::abs((kind==DimensionKind::DistanceX?updated->x:updated->y)-value)<1e-7,"Fillet center coordinate equation failed");
                    equations(placed,added.arc_id,real.a,real.b,1.);
                }
            }
            const auto before=placed.serialized();const auto p=*placed.find_point(center_id);
            check(!placed.move_point(center_id,p.x+1,p.y+1)&&placed.serialized()==before,"Locked center escaped coordinate dimensions");
        }
        {
            auto equal=real.sketch;const auto reference=equal.add_circle(40,40,1.);
            SketchConstraint c;c.id="fillet-equal-radius";c.kind=ConstraintKind::EqualRadius;
            c.geometry_id=reference;c.second_geometry_id=added.arc_id;equal.constraints.push_back(c);
            SketchDimension d;d.id="reference-circle-size";d.geometry_id=reference;d.kind=DimensionKind::Radius;d.value=1.;d.locked=true;equal.dimensions.push_back(d);
            for(double radius:{.8,1.2,1.}) {
                check(equal.set_dimension_value(d.id,radius),"Equal Radius could not drive a fillet");
                equations(equal,added.arc_id,real.a,real.b,radius);
            }
        }
        const auto before=f.sketch.serialized();bool rejected{};
        try{static_cast<void>(f.sketch.add_corner_fillet(f.a,f.b,-1));}catch(const std::exception&){rejected=true;}
        check(rejected&&f.sketch.serialized()==before,"Impossible radius changed the Sketch");
        ++cases;
    }
    {
        auto separate=Sketch::create_default();
        const auto a=separate.add_segment(1,0,25,0),b=separate.add_segment(0,1,0,20);
        separate.constraints.clear();
        const auto fillet=fillet_sketch_curves(separate,a,b,2.);
        equations(separate,fillet.arc_id,a,b,2.);
    }
    {
        auto imported=fixture(0,0);
        auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        reference.source_document_id="read-only-source";
        reference.source_owner_id="native-line";
        reference.source_semantic_key="segment:source";
        reference.cached_points={{0,0},{25,0}};
        const auto reference_id=reference.id;
        imported.sketch.external_references.push_back(reference);
        const auto endpoints=*fillet_curve_endpoints(imported.sketch,imported.a);
        static_cast<void>(imported.sketch.add_import_block("read-only", "external-reference:"+reference_id,
            {imported.a},{endpoints[0],endpoints[1]}));
        const auto before=imported.sketch.serialized();bool rejected=false;
        try{static_cast<void>(fillet_sketch_curves(imported.sketch,imported.a,imported.b,1.));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&imported.sketch.serialized()==before,"Fillet modified a read-only imported support");
    }
    std::cout<<cases<<" curve fillet cases passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
