#include <zima/symbols/definition.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/document_session.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <numbers>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,const char* message){check(std::abs(a-b)<1e-5,message);}
sketcher::SymbolInstance symbol(double x=30,double y=40) {
    auto definition=symbols::projection_method();sketcher::SymbolInstance result;
    result.id="projection";result.definition=definition.serialized();result.variant=definition.default_variant;
    result.x=x;result.y=y;return result;
}
std::string add_grip(sketcher::Sketch& sketch,int type,double x=30,double y=40) {
    if(type==1){sketch.add_symbol(symbol(x,y));return sketch.symbols.front().anchor_point_id;}
    if(type==2){auto text=sketcher::Sketch::create_text();text.value="Anchor";text.modeling_geometry=false;
        text.anchor_x=x;text.anchor_y=y;text.contours={{{x,y},{x+2,y},{x+2,y+3},{x,y+3}}};
        sketch.add_text(std::move(text));return sketch.texts.front().anchor_point_id;}
    return sketch.add_point(x,y);
}
void anchor(const sketcher::Sketch& sketch) {
    if(sketch.symbols.empty()&&sketch.texts.empty())return;
    const auto mesh=sketch.viewer_mesh();
    const auto offered=[&](const auto& id){check(std::ranges::any_of(mesh.points,[&](const auto& point){return point.reference.owner_id==sketch.id&&point.reference.semantic_key=="point:"+id;}),"Anchor is not offered as a real Sketch point");};
    for(const auto& instance:sketch.symbols){const auto* p=sketch.find_point(instance.anchor_point_id);
        check(p,"Symbol has no ordinary native point");near(p->x,instance.x,"Symbol X detached from solver point");near(p->y,instance.y,"Symbol Y detached from solver point");offered(p->id);}
    for(const auto& text:sketch.texts){const auto* p=sketch.find_point(text.anchor_point_id);
        check(p,"Text has no ordinary native point");near(p->x,text.anchor_x,"Text X detached from solver point");near(p->y,text.anchor_y,"Text Y detached from solver point");
        near(text.contours.front().front()[0],p->x,"Text contour X detached from its anchor");near(text.contours.front().front()[1],p->y,"Text contour Y detached from its anchor");offered(p->id);}
}
void equation(const sketcher::Sketch& sketch) {
    anchor(sketch);
    for(const auto& d:sketch.dimensions)if(d.driving&&!d.suppressed) {
        const auto* a=sketch.find_point(d.first_point_id);const auto* b=sketch.find_point(d.second_point_id);
        if(!a||!b)continue;
        const double actual=d.kind==sketcher::DimensionKind::DistanceX?std::abs(a->x-b->x):
            d.kind==sketcher::DimensionKind::DistanceY?std::abs(a->y-b->y):std::hypot(a->x-b->x,a->y-b->y);
        near(actual,std::abs(d.value),"Independent point-dimension equation failed");
    }
}
}
int main(){try {
    std::size_t cases{};
    for(int type:{0,1,2})
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(double side:{-1.,1.})for(bool reverse:{false,true})
    for(const auto kind:{sketcher::DimensionKind::DistanceX,sketcher::DimensionKind::DistanceY,sketcher::DimensionKind::Distance})
    for(bool locked:{false,true})for(bool driving:{false,true}) {
        if(locked&&!driving)continue; // Reference dimensions cannot be locked.
        auto sketch=sketcher::Sketch::create_default();sketch.plane=plane;
        const auto grip=add_grip(sketch,type,side*30,side*40);
        const auto fixed=sketch.add_point(0,0);sketch.set_point_fixed(fixed,true);
        auto dim=sketch.create_point_dimension(reverse?grip:fixed,reverse?fixed:grip,kind);dim.locked=locked;dim.driving=driving;
        sketch.apply_dimension(dim);equation(sketch);
        for(int edit=0;edit<3;++edit) {
            if(driving)check(sketch.set_dimension_value(dim.id,50+edit*10),"Successive symbol dimension edit failed");
            equation(sketch);
            const auto before=*sketch.find_point(grip);
            check(sketch.move_point(grip,side*(70+edit*5),side*(90+edit*5)),"A free symbol degree of freedom did not move");
            check(std::hypot(sketch.find_point(grip)->x-before.x,sketch.find_point(grip)->y-before.y)>1e-5,"Symbol drag reported success without movement");
            equation(sketch);near(sketch.find_point(fixed)->x,0,"Fixed support moved");near(sketch.find_point(fixed)->y,0,"Fixed support moved");
        }
        const auto serialized=sketch.serialized();sketch=sketcher::Sketch::from_serialized(serialized);
        check(sketch.find_point(grip),"Reopen changed anchor identity");equation(sketch);++cases;
    }
    for(int type:{0,1,2})for(double side:{-1.,1.})for(bool reverse:{false,true}) {
        auto fixed=sketcher::Sketch::create_default();const auto point=add_grip(fixed,type,side*30,side*40);
        const auto origin=fixed.add_point(0,0);fixed.set_point_fixed(origin,true);
        auto length=fixed.create_point_dimension(reverse?point:origin,reverse?origin:point,sketcher::DimensionKind::Distance);length.locked=true;fixed.apply_dimension(length);
        auto ordinate=fixed.create_axis_dimension(point,"sketch_axis:x");ordinate.locked=true;fixed.apply_dimension(ordinate);
        for(double value:{55.,65.,45.}) {
            check(fixed.set_dimension_value(length.id,value),"Fully constrained aligned distance cannot be edited");anchor(fixed);
            const auto before=fixed.serialized();const auto* p=fixed.find_point(point);
            for(const auto delta:{std::array{5.,3.},std::array{-8.,4.},std::array{2.,-7.}})
                check(!fixed.move_point(point,p->x+delta[0],p->y+delta[1])&&fixed.serialized()==before,"Fully constrained aligned drag normalized native data");
        }
    }
    // Fully constrained anchors reject dragging and conflicting Properties,
    // with no document mutation. Rotation and scale remain independent.
    auto sketch=sketcher::Sketch::create_default();sketch.add_symbol(symbol());const auto grip=sketch.symbols.front().anchor_point_id;
    sketch.set_point_fixed(grip,true);auto before=sketch.serialized();
    check(!sketch.move_point(grip,99,99)&&sketch.serialized()==before,"Fixed symbol escaped its constraints");
    auto changed=sketch.symbols.front();changed.x=99;bool rejected=false;
    try{sketch.update_symbol(changed);}catch(const std::exception&){rejected=true;}
    check(rejected&&sketch.serialized()==before,"Conflicting Properties changed the Sketch");
    changed=sketch.symbols.front();changed.angle_degrees=35;changed.scale=2;sketch.update_symbol(changed);anchor(sketch);
    check(sketch.symbols.front().anchor_point_id==grip,"Properties replaced anchor identity");
    auto fixed_symbol=sketch;fixed_symbol.remove_symbol(fixed_symbol.symbols.front().id);
    check(fixed_symbol.find_point(grip)&&fixed_symbol.find_point(grip)->fixed,"Symbol deletion erased an independently fixed point");
    {
        auto constrained=sketcher::Sketch::create_default();constrained.add_symbol(symbol());const auto point=constrained.symbols.front().anchor_point_id;
        auto x=constrained.create_axis_dimension(point,"sketch_axis:y"),y=constrained.create_axis_dimension(point,"sketch_axis:x");x.locked=y.locked=true;
        constrained.apply_dimension(x);constrained.apply_dimension(y);anchor(constrained);
        auto previous=constrained.serialized();
        check(!constrained.move_point(point,100,100)&&constrained.serialized()==previous,"Two coordinate dimensions did not constrain the symbol");
        for(double value:{15.,0.,-20.}) {
            check(constrained.set_dimension_value(x.id,value),"Signed symbol coordinate edit failed");anchor(constrained);
            near(constrained.find_point(point)->x,value,"Signed symbol coordinate equation failed");
            previous=constrained.serialized();check(!constrained.move_point(point,100,100)&&constrained.serialized()==previous,"Dimension edit released a fully constrained symbol");
        }
    }
    for(int type:{1,2})for(bool external:{false,true})for(double side:{-1.,1.})for(double angle:{0.,37.,90.})for(int state:{0,1,2}) {
        const double radians=angle*std::numbers::pi/180.,c=std::cos(radians),s=std::sin(radians);
        const auto xy=[&](double x,double y){return std::array{x*c-y*s,x*s+y*c};};
        const auto initial=xy(30,side*20);
        auto supported=sketcher::Sketch::create_default();const auto point=add_grip(supported,type,initial[0],initial[1]);
        std::string line;
        if(external) {
            auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Axis);
            reference.source_document_id="external-part";reference.source_owner_id="external-axis";reference.source_semantic_key="axis";reference.cached_points={xy(-100,0),xy(100,0)};
            line=reference.id;supported.add_external_reference(reference);
        } else {
            const auto a=xy(-100,0),b=xy(100,0);line=supported.add_segment(a[0],a[1],b[0],b[1]);const auto segment=supported.segments.back();
            supported.set_point_fixed(segment.first_point_id,true);supported.set_point_fixed(segment.second_point_id,true);
        }
        const auto references=supported.external_references;
        auto dimension=supported.create_point_line_dimension(point,line);dimension.driving=state!=0;dimension.locked=state==2;supported.apply_dimension(dimension);
        for(double distance:{25.,35.,15.}) {
            if(dimension.driving)check(supported.set_dimension_value(dimension.id,distance),"Point-to-line symbol dimension edit failed");anchor(supported);
            const auto* p=supported.find_point(point);
            if(dimension.driving)near(std::abs(-s*p->x+c*p->y),distance,"Independent symbol point-to-line equation failed");
            const auto previous=*p;const double along=c*p->x+s*p->y;const auto target=xy(along+10,side*distance);
            check(supported.move_point(point,target[0],target[1]),"Symbol cannot slide along its distance support");anchor(supported);
            p=supported.find_point(point);check(std::hypot(p->x-previous.x,p->y-previous.y)>1e-5,"Point-to-line drag did not move");
            if(dimension.locked)near(std::abs(-s*p->x+c*p->y),distance,"Sliding violated the point-to-line dimension");
            check(supported.external_references==references,"Symbol drag modified its external source");
        }
        supported=sketcher::Sketch::from_serialized(supported.serialized());anchor(supported);
        ++cases;
    }
    // A native point also participates in angular and symmetric dimensions.
    for(int type:{0,1,2})for(int kind:{0,1,2,3})for(int state:{0,1,2})for(bool reverse:{false,true})for(double side:{-1.,1.}) {
        auto angular=sketcher::Sketch::create_default();const auto grip=add_grip(angular,type,30,side*40);
        const auto origin=angular.add_point(0,0),peer=angular.add_point(50,0);
        angular.set_point_fixed(origin,true);angular.set_point_fixed(peer,true);
        const auto line=angular.add_segment(0,0,50,0);
        auto dimension=kind==0?angular.create_three_point_angle_dimension(reverse?grip:peer,origin,reverse?peer:grip):
            kind==1?angular.create_point_line_angle_dimension(reverse?grip:origin,reverse?origin:grip,line):
            kind==2?angular.create_four_point_angle_dimension(origin,reverse?grip:peer,origin,reverse?peer:grip):
                angular.create_symmetric_dimension(grip,{},reverse?"sketch_axis:x":"sketch_axis:y");
        dimension.driving=state!=0;dimension.locked=state==2;angular.apply_dimension(dimension);anchor(angular);
        for(double value:{35.,55.,25.}) {
            if(dimension.driving&&!angular.set_dimension_value(dimension.id,value))throw std::runtime_error("Angular/symmetric edit: kind="+std::to_string(kind)+" state="+std::to_string(state)+" reverse="+std::to_string(reverse)+" side="+std::to_string(side)+" value="+std::to_string(value));
            anchor(angular);auto p=*angular.find_point(grip);
            const auto verify_equation=[&] {
                const auto* q=angular.find_point(grip);const auto& d=angular.dimensions.front();
                if(!d.driving)return;
                if(kind==3)near(2*std::abs(reverse?q->y:q->x),std::abs(d.value),"Independent symmetric point equation failed");
                else {
                    const double sign=kind==1&&reverse?-1.:1.;
                    near(sign*q->x/std::hypot(q->x,q->y),std::cos(d.value*std::numbers::pi/180.),"Independent angular point equation failed");
                }
            };
            verify_equation();
            // Scaling about the vertex remains on the angular free ray. A
            // symmetric coordinate retains its normal and slides tangentially.
            const auto target=kind==3?std::array{p.x+(reverse?7:0),p.y+(reverse?0:7)}:std::array{p.x*1.2,p.y*1.2};
            check(angular.move_point(grip,target[0],target[1]),"Angular/symmetric free degree of freedom did not move");anchor(angular);
            const auto* q=angular.find_point(grip);check(std::hypot(q->x-p.x,q->y-p.y)>1e-5,"Angular/symmetric drag reported no movement");
            if(dimension.locked)verify_equation();
            near(angular.find_point(origin)->x,0,"Angular support moved");near(angular.find_point(peer)->x,50,"Angular support moved");
        }
        angular=sketcher::Sketch::from_serialized(angular.serialized());anchor(angular);
        if(type) {
            const auto dimensions=angular.dimensions;
            if(type==1)angular.remove_symbol(angular.symbols.front().id);else angular.remove_geometry(angular.texts.front().id);
            check(angular.find_point(grip)&&angular.dimensions==dimensions,"Deleting an angular/symmetric symbol discarded its independent point dimension");angular.validate();
        }
        ++cases;
    }
    for(int type:{1,2}) {
        auto bound=sketcher::Sketch::create_default();const auto point=add_grip(bound,type);
        auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
        reference.source_document_id="external-part";reference.source_owner_id="external-point";reference.source_semantic_key="point";reference.cached_points={{2,3}};
        const auto id=reference.id;bound.add_external_reference(reference);
        static_cast<void>(bound.add_point_reference_constraint(point,id));anchor(bound);
        near(bound.find_point(point)->x,2,"External point did not position anchor X");near(bound.find_point(point)->y,3,"External point did not position anchor Y");
        const auto before=bound.serialized();check(!bound.move_point(point,99,99)&&bound.serialized()==before,"Symbol escaped its external point constraint");
    }
    // Merging either selection order retains native incidence. Removing a
    // connected curve must preserve the symbol point and its identity.
    for(int type:{1,2})for(bool reverse:{false,true}) {
        auto merged=sketcher::Sketch::create_default();const auto point=add_grip(merged,type,5,5);
        const auto line=merged.add_segment(7,5,20,5);const auto endpoint=merged.segments.front().first_point_id;
        const auto survivor=merged.merge_points(reverse?endpoint:point,reverse?point:endpoint);
        check((type==1?merged.symbols.front().anchor_point_id:merged.texts.front().anchor_point_id)==survivor,"Point merge lost anchor incidence");anchor(merged);
        auto trimmed=merged;static_cast<void>(trimmed.retain_curve_intervals(line,{{.5,1.}}));
        check(trimmed.find_point(survivor),"Trimming erased an independent text/symbol anchor");anchor(trimmed);
        merged.remove_geometry(line);check(merged.find_point(survivor),"Removing a curve erased its symbol anchor");anchor(merged);
        merged.remove_point(survivor);check(merged.texts.empty()&&merged.symbols.empty(),"Deleting an anchor left orphaned annotation geometry");
    }
    // Shared Part and Assembly mutation paths retain Undo/Redo and native data.
    const auto directory=std::filesystem::temp_directory_path()/("zima-symbol-anchor-"+sketch.id);std::filesystem::create_directory(directory);
    for(int type:{1,2})for(bool assembly:{false,true}) {
        auto editable=sketcher::Sketch::create_default();const auto point=add_grip(editable,type);
        auto owner=document::PartDocument::create_sketch_container();editable.owner_container_id=owner.id;
        workspace::Workspace live;std::string id;std::filesystem::path path;
        if(assembly) {auto doc=assembly::AssemblyDocument::create_default();doc.sketches={editable};doc.sketch_containers={owner};id=doc.document_id;path=directory/"anchor.asmz";live.add_assembly(doc,path);}
        else {auto doc=document::PartDocument::create_default();doc.sketches={editable};doc.history={owner};id=doc.document_id;path=directory/"anchor.prtz";live.add_part(doc,{},path);}
        const auto apply=[&](auto& pending){check(pending.move_point(point,60,70),"Workspace anchor move failed");};
        check(workspace::mutate_document_sketch(live,id,editable.id,apply),"Symbol move did not create a transaction");
        if(assembly) {auto& session=live.open_assembly(id)->session;check(session.undo()&&session.redo(),"Assembly anchor Undo/Redo failed");session.document().save(path);anchor(assembly::AssemblyDocument::load(path).sketches.front());}
        else {auto& session=live.open_part(id)->session;check(session.undo()&&session.redo(),"Part anchor Undo/Redo failed");session.document().save(path);anchor(document::PartDocument::load(path).sketches.front());}
    }
    std::filesystem::remove_all(directory);
    std::cout<<cases<<" point/symbol/text dimension/drag variants, fixed rejection, merge, trim, persistence and Undo/Redo passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
