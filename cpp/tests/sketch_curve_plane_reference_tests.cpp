#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/curve_plane_intersection.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <nlohmann/json.hpp>

using namespace zima;
using namespace sketcher;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>=1e-6)throw std::runtime_error("Curve-plane independent coordinate equation failed: got "+std::to_string(a)+", expected "+std::to_string(b));}
void solved(Sketch& s){const auto r=s.solve();check(r.status!=SolveStatus::Conflicting&&r.status!=SolveStatus::Invalid,"Curve-plane supported Sketch cannot solve");}
struct Fixture {document::PartDocument part;std::vector<kernel::BodyResult> bodies;std::string curve;};
Fixture fixture(const kernel::OcctKernel& engine,bool placed=false,bool separate=false) {
    Fixture f;f.part=document::PartDocument::create_default();auto stock=Sketch::create_default();
    static_cast<void>(stock.add_rectangle(-5,-5,5,5));auto solid=document::PartDocument::create_feature_container(stock.id);
    solid.feature.sides[0].length=2;stock.owner_container_id=solid.id;
    auto source=Sketch::create_default();auto source_owner=document::PartDocument::create_sketch_container();source_owner.feature.sketch_id=source.id;source.owner_container_id=source_owner.id;
    f.curve=source.add_bspline({{0,-.08},{1./3,.14},{2./3,-.14},{1,.08}},3,false,false,1e-6,false);
    auto target=Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();owner.feature.sketch_id=target.id;target.owner_container_id=owner.id;
    target.plane=SketchPlane::XZ;target.plane_auto=false;
    f.part.history={solid,source_owner,owner};f.part.sketches={stock,source,target};
    document::BodyHistoryGraph graph;const auto body=graph.create_body("Curve source");
    graph.insert({document::PartHistoryKind::Feature,solid.id});graph.insert({document::PartHistoryKind::Feature,source_owner.id});
    if(placed){auto entry=*graph.find(body);entry.scope.placement.x=12;entry.scope.placement.y=-7;entry.scope.placement.z=9;
        entry.scope.placement.rotation_x=entry.scope.placement.absolute_rotation_x=90;graph.update_body(entry);}
    if(separate){static_cast<void>(graph.create_body("Curve target"));if(placed){f.part.sketches.back().plane=SketchPlane::XY;f.part.history.back().placement.z=9;}}
    graph.insert({document::PartHistoryKind::Feature,owner.id});f.part.set_body_history(graph);f.part.resolve_constructions();
    f.bodies=engine.evaluate_history(f.part.kernel_operations());check(!f.bodies.empty()&&f.bodies.back().calculation_errors.empty(),"Curve reference fixture failed to calculate");return f;
}
std::vector<std::array<double,2>> poles(double middle) {
    const double sum=.2+middle+.8,pairs=.2*middle+.16+middle*.8,product=.16*middle;
    return {{0,-product},{1./3,-product+pairs/3},{2./3,-product+2*pairs/3-sum/3},{1,1-sum+pairs-product}};
}
void refresh(Fixture& f) {
    auto& target=f.part.sketches.back();const auto values=workspace::calculate_part_sketch_curve_intersections(document::DocumentSession(f.part,f.bodies),target);
    check(values.size()==target.external_references.size(),"Curve intersection refresh omitted a binding");
    static_cast<void>(target.refresh_external_references(f.part.document_id,{},false,nullptr,false,&values));
}
void part_cases(const kernel::OcctKernel& engine,const std::filesystem::path& directory) {
    for(bool placed:{false,true})for(bool separate:{false,true}) {
        auto f=fixture(engine,placed,separate);workspace::Workspace live;live.add_part(f.part,f.bodies);live.activate(f.part.document_id);
        const auto unchanged=f.part.serialized();const auto generation=live.open_part(f.part.document_id)->session.revision();
        auto& target=f.part.sketches.back();const kernel::EdgeReference edge{f.part.sketches[1].id,"bspline:"+f.curve,{}};
        const auto empty_target=target;
        const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,edge);
        check(branches.size()==3,"Exact Sketch spline did not offer all three isolated points");
        check(live.open_part(f.part.document_id)->session.document().serialized()==unchanged&&live.open_part(f.part.document_id)->session.revision()==generation,"Explicit curve query modified source or Undo history");
        std::set<std::string> keys;
        for(std::size_t i=0;i<3;++i){const auto& r=branches[i];check(r.kind==ExternalReferenceKind::Point&&r.curve_intersection&&!r.intersection&&!r.exact_spline&&r.cached_points.size()==1,"Curve intersection is not a single native Point");
            check(keys.insert(r.curve_intersection->branch_key).second,"Curve intersection branches share identity");
            near(r.cached_points.front()[0],(placed&&separate?12:0)+std::array{.2,.5,.8}[i]);near(r.cached_points.front()[1],placed&&separate?-7:0);
            target.add_external_reference(r);auto p=Sketch::create_point(r.cached_points.front()[0],r.cached_points.front()[1]);
            const auto id=p.id;target.points.push_back(std::move(p));static_cast<void>(target.add_point_reference_constraint(id,r.id));}
        check(target.points.size()==3&&target.constraints.size()==3,"Point contour did not create exactly three linked native points");
        const auto bindings=target.external_references;const auto point_ids=target.points;
        for(double middle:{.55,.45,.52,.5}) {
            f.part.sketches[1].edit_bspline_properties(f.curve,3,false,poles(middle));refresh(f);
            for(std::size_t i=0;i<3;++i){const auto& r=target.external_references[i];check(!r.broken&&r.curve_intersection==bindings[i].curve_intersection&&r.id==bindings[i].id,"Source edit rebound or broke a certified curve branch");
                near(r.cached_points.front()[0],(placed&&separate?12:0)+std::array{.2,middle,.8}[i]);}
            solved(target);near(target.points[1].x,(placed&&separate?12:0)+middle);
            check(target.points[1].id==point_ids[1].id,"Source edit replaced native point identity");
        }
        const auto stable=target.external_references;
        auto absent=poles(.5);for(auto& p:absent)p[1]+=10;f.part.sketches[1].edit_bspline_properties(f.curve,3,false,absent);refresh(f);
        for(std::size_t i=0;i<3;++i)check(target.external_references[i].broken&&target.external_references[i].cached_points==stable[i].cached_points,"Missing intersection moved frozen point or rebound a neighbour");
        f.part.sketches[1].edit_bspline_properties(f.curve,3,false,poles(.5));refresh(f);
        for(const auto& r:target.external_references)check(!r.broken,"Restored certified branch did not recover");
        f.part.sketches[1].edit_bspline_properties(f.curve,3,false,{{0,-.12},{1./3,-.12+.74/3},{2./3,-.12+2*.74/3-.5},{1,.12}});refresh(f);
        check(target.external_references[1].broken&&target.external_references[1].cached_points==stable[1].cached_points,
            "Ambiguous retained parameter interval guessed one of three roots");
        f.part.sketches[1].edit_bspline_properties(f.curve,3,false,poles(.5));refresh(f);
        auto& source=f.part.sketches[1].bsplines.front();std::ranges::reverse(source.control_point_ids);refresh(f);
        for(const auto& r:target.external_references)check(r.broken,"Reparameterized source silently rebound curve branch");
        std::ranges::reverse(source.control_point_ids);refresh(f);
        const auto file=directory/("curve-point-"+std::to_string(placed)+std::to_string(separate)+".prtz");f.part.save(file,f.bodies);
        auto baseline=f.part;baseline.sketches.back()=empty_target;const auto baseline_file=directory/("baseline-"+std::to_string(placed)+std::to_string(separate)+".prtz");baseline.save(baseline_file,f.bodies);
        std::cout<<"Native binding bytes frame "<<placed<<separate<<": before="<<std::filesystem::file_size(baseline_file)
            <<" after="<<std::filesystem::file_size(file)<<" delta="<<(std::int64_t(std::filesystem::file_size(file))-std::int64_t(std::filesystem::file_size(baseline_file)))<<'\n';
        std::vector<kernel::BodyResult> saved;const auto reopened=document::PartDocument::load(file,&saved);
        check(reopened.sketches.back().serialized()==target.serialized(),"Native Part lost exact point binding/cache");
        const auto cold=workspace::calculate_part_sketch_curve_intersections(document::DocumentSession(reopened,saved),reopened.sketches.back());
        check(cold.size()==3&&std::ranges::none_of(cold,[](const auto& r){return r.broken;}),"Cold reopen lost curve intersection sources");
        document::DocumentSession session(f.part,f.bodies);auto changed=f.part;changed.sketches.back().remove_geometry(bindings[1].id);session.commit(changed,f.bodies);
        check(session.undo()&&session.document().sketches.back().serialized()==target.serialized(),"Point binding Undo lost original Sketch");
        check(session.redo()&&session.document().sketches.back().external_references.size()==2,"Point binding Redo did not remove only the selected root");
    }
    std::cout<<"Four Part/body frames: three exact roots, successive source edits, loss/recovery, parameterization rejection, native save/reopen and Undo/Redo passed\n";
}
void dimension_cases(const kernel::OcctKernel& engine) {
    const auto f=fixture(engine);workspace::Workspace live;live.add_part(f.part,f.bodies);const auto& target=f.part.sketches.back();
    const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,{f.part.sketches[1].id,"bspline:"+f.curve,{}});
    const auto reference=branches[1];unsigned cases{},edits{},moves{};
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})for(bool order:{false,true})for(double sign:{-1.,1.})for(bool driving:{false,true})for(bool locked:{false,true})for(bool fixed:{false,true}) {
        auto s=target;s.add_external_reference(reference);const auto native=s.add_point(.5+4*sign,3*sign);s.set_point_fixed(native,fixed);
        auto d=s.create_point_dimension(order?reference.id:native,order?native:reference.id,kind);d.driving=driving;d.locked=locked;
        const auto before=s.serialized();bool applied=true;std::string failure;
        try{s.apply_dimension(d);}catch(const std::exception& e){applied=false;failure=e.what();}
        if(!driving&&locked){check(!applied&&before==s.serialized(),"Locked point reference dimension was not atomic");++cases;continue;}
        if(driving&&fixed){check(!applied&&before==s.serialized(),"Redundant fixed point driver was not rejected atomically");++cases;continue;}
        if(!applied)throw std::runtime_error("Valid point intersection dimension was rejected: kind="+std::to_string(static_cast<int>(kind))+
            " order="+std::to_string(order)+" sign="+std::to_string(sign)+" driving="+std::to_string(driving)+
            " locked="+std::to_string(locked)+" fixed="+std::to_string(fixed)+": "+failure);
        const auto verify=[&]{const auto* p=s.find_point(native);const auto q=reference.cached_points.front();const double dx=(p->x-q[0])*(order?1:-1),dy=(p->y-q[1])*(order?1:-1);
            const double actual=kind==DimensionKind::Distance?std::hypot(dx,dy):kind==DimensionKind::DistanceX?dx:dy;
            const auto& persisted=s.dimensions.front();near(actual,persisted.value);check(s.external_references==std::vector{reference},"Point dimension or drag modified read-only source");};
        verify();
        for(double factor:{1.05,.97,1.02}) {const auto unchanged=s.serialized();const auto p=*s.find_point(native);const bool accepted=s.set_dimension_value(d.id,dimension_display_value(d)*factor);
            if(driving&&fixed)check(!accepted&&unchanged==s.serialized(),"Impossible fixed point edit changed Sketch");
            else {check(accepted,"Editable point dimension rejected");if(!driving)check(unchanged==s.serialized(),"Reference point dimension changed geometry");else check(std::hypot(s.find_point(native)->x-p.x,s.find_point(native)->y-p.y)>.001,"Driving point dimension did not move available DOF");}
            solved(s);verify();s=Sketch::from_serialized(s.serialized());++edits;}
        for(int i=0;i<3;++i){const auto p=*s.find_point(native);const auto unchanged=s.serialized();double x=p.x+.2,y=p.y+.3;
            if(driving&&locked) {
                if(kind==DimensionKind::DistanceX)x=p.x;
                else if(kind==DimensionKind::DistanceY)y=p.y;
                else {const auto q=reference.cached_points.front();const double dx=p.x-q[0],dy=p.y-q[1];x=q[0]+dx*std::cos(.05)-dy*std::sin(.05);y=q[1]+dx*std::sin(.05)+dy*std::cos(.05);}
            }
            const bool moved=s.move_point(native,x,y);
            if(fixed)check(!moved&&unchanged==s.serialized(),"Fixed supported point escaped on drag");
            else {check(moved,"Point drag along an available constrained DOF was rejected");check(std::hypot(s.find_point(native)->x-p.x,s.find_point(native)->y-p.y)>.001,"Point drag accepted without movement");++moves;}
            solved(s);verify();}++cases;
    }
    std::cout<<cases<<" external intersection point dimension states, "<<edits<<" successive edits and "<<moves<<" verified DOF drags passed\n";
}
void authored_3d(const kernel::OcctKernel& engine) {
    auto f=fixture(engine);auto object=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    object.curve_type=document::Curve3DType::InterpolatingSpline;
    for(int i=0;i<4;++i){auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);point.parent_construction_id=object.id;point.origin={double(i),0,i%2?1.:-1.};object.curve_points.push_back(point);}
    f.part.constructions={object};auto graph=f.part.body_history;graph.set_history_cursor(graph.active_body_id(),2);
    graph.insert({document::PartHistoryKind::Construction,object.id});f.part.set_body_history(graph);
    f.part.sketches.back().plane=SketchPlane::XY;f.part.resolve_constructions();
    const auto route=document::curve3d_route(f.part.constructions.front());check(route.segments.size()==3,"3D spline fixture route is missing");
    workspace::Workspace live;live.add_part(f.part,f.bodies);const auto& target=f.part.sketches.back();
    const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,{object.entity_id,route.segments[1].source_id,{}});
    check(branches.size()==1&&branches.front().curve_intersection,"Authored 3D curve did not create an associative point");near(branches.front().cached_points.front()[0],1.5);near(branches.front().cached_points.front()[1],0);
    std::cout<<"Exact authored interpolating 3D curve segment / Sketch plane point passed\n";
}
void analytic_sources(const kernel::OcctKernel& engine) {
    for(int kind=0;kind<4;++kind) {
        std::cerr<<"Analytic source kind "<<kind<<" begin\n";
        auto f=fixture(engine);auto& source=f.part.sketches[1];source.bsplines.clear();source.points.clear();
        std::string key;
        if(kind==0)key="circle:"+source.add_circle(0,0,5);
        else if(kind==1)key="ellipse:"+source.add_ellipse(0,0,5,0,0,3);
        else if(kind==2)key="arc:"+source.add_arc(0,0,5,0,-5,0);
        else key="segment:"+source.add_segment(-5,-3,5,3);
        workspace::Workspace live;live.add_part(f.part,f.bodies);auto target=f.part.sketches.back();
        const auto values=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,{source.id,key,{}});
        std::cerr<<"Analytic source kind "<<kind<<" roots="<<values.size()<<'\n';
        check(values.size()==(kind==3?1u:2u),"Analytic source did not offer its exact bounded contacts");
        std::vector<double> xs;for(const auto& r:values){xs.push_back(r.cached_points.front()[0]);near(r.cached_points.front()[1],0);target.add_external_reference(r);}
        std::ranges::sort(xs);if(kind==3)near(xs.front(),0);else {near(xs.front(),-5);near(xs.back(),5);}
        const auto cold=Sketch::from_serialized(target.serialized());
        const auto refreshed=workspace::calculate_part_sketch_curve_intersections(document::DocumentSession(f.part,f.bodies),cold);
        check(refreshed.size()==values.size()&&std::ranges::none_of(refreshed,[](const auto& r){return r.broken;}),"Analytic curve binding did not survive native Sketch roundtrip");
        if(kind==1) {
            auto& ellipse=source.ellipses.front();ellipse.rotation=std::acos(-1.);
            for(const auto& id:{ellipse.major_point_id,ellipse.minor_point_id}){auto* point=source.find_point(id);point->x=-point->x;point->y=-point->y;}
            const auto changed=workspace::calculate_part_sketch_curve_intersections(document::DocumentSession(f.part,f.bodies),cold);
            check(changed.size()==2&&std::ranges::all_of(changed,[](const auto& r){return r.broken;}),
                "A reversed native crossing orientation silently swapped the two ellipse roots");
            for(std::size_t i=0;i<2;++i)check(changed[i].cached_points==values[i].cached_points,"Rejected ellipse branch moved its frozen point");
        }
    }
    auto f=fixture(engine);f.part.sketches.back().plane=SketchPlane::XY;f.part.resolve_constructions();
    const auto& source=f.part.sketches[1];const auto& target=f.part.sketches.back();const auto id=f.curve;
    workspace::Workspace live;live.add_part(f.part,f.bodies);const auto before=f.part.serialized();bool rejected=false;
    try{const auto values=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,{source.id,"bspline:"+id,{}});
        std::cerr<<"Coplanar query unexpectedly returned "<<values.size()<<" point(s)\n";}
    catch(const workspace::SketchOperationError& e){rejected=std::string(e.code)=="non_unique_intersection";
        if(!rejected)std::cerr<<"Coplanar query rejected with "<<e.code<<": "<<e.what()<<'\n';}
    check(rejected&&before==f.part.serialized(),"Coplanar source interval did not reject non-unique point atomically");
    std::cout<<"Native circle, ellipse, bounded arc endpoints, segment, closed seam bindings and coplanar rejection passed\n";
}
void assembly_cases(const kernel::OcctKernel& engine,const std::filesystem::path& directory) {
    for(bool nested:{false,true}) {
        std::cerr<<"Assembly curve case nested="<<nested<<" begin\n";
        auto f=fixture(engine);const auto file=directory/(nested?"nested-source.prtz":"source.prtz");f.part.save(file,f.bodies);
        auto top=assembly::AssemblyDocument::create_default();
        auto occurrence=assembly::AssemblyDocument::create_part_occurrence("Source",f.part.document_id,file,f.bodies.back());
        occurrence.placement.x=20;occurrence.placement.y=-10;
        auto path=assembly::InstancePath{}.child(occurrence.occurrence_id);
        workspace::Workspace live;live.add_part(f.part,f.bodies,file);
        if(nested) {
            auto sub=assembly::AssemblyDocument::create_default();sub.components={occurrence};
            sub.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,occurrence.occurrence_id);
            const auto sub_file=directory/"curve-sub.asmz";sub.save(sub_file);
            live.add_assembly(sub,sub_file);
            auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Sub",sub.document_id,sub_file,sub);parent.placement={5,7,9,0,0,0};
            top.components={parent};top.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,parent.occurrence_id);
            path=assembly::InstancePath{}.child(parent.occurrence_id).child(occurrence.occurrence_id);
        } else {
            auto repeat=occurrence;repeat.occurrence_id=Sketch::create_default().id;repeat.placement.x+=100;
            top.components={occurrence,repeat};for(const auto& c:top.components)top.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,c.occurrence_id);
        }
        auto target=Sketch::create_default();target.plane=SketchPlane::XZ;target.plane_auto=false;
        auto owner=document::PartDocument::create_sketch_container();target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;
        owner.placement.x=nested?25:20;owner.placement.y=nested?-3:-10;owner.placement.z=nested?9:0;
        top.insert_sketch(target,owner);top.resolve_constructions();target=top.sketches.back();live.add_assembly(top);live.activate(top.document_id);
        const auto revision=live.open_assembly(top.document_id)->session.revision();
        const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,top.document_id,target,{f.part.sketches[1].id,"bspline:"+f.curve,path.encoded()});
        check(branches.size()==3,"Assembly occurrence did not offer three source curve points");
        for(std::size_t i=0;i<3;++i){near(branches[i].cached_points.front()[0],std::array{.2,.5,.8}[i]);near(branches[i].cached_points.front()[1],0);
            check(branches[i].source_instance_path==path.encoded(),"Repeated Assembly occurrence lost exact source identity");target.add_external_reference(branches[i]);}
        check(live.open_assembly(top.document_id)->session.revision()==revision,"Assembly query changed history or calculated mates");
        top.sketches.back()=target;const auto top_file=directory/(nested?"nested-top.asmz":"top.asmz");top.save(top_file);
        auto values=workspace::calculate_context_sketch_curve_intersections(live,top.document_id,target);
        check(values.size()==3&&std::ranges::none_of(values,[](const auto& r){return r.broken;}),"Assembly curve refresh lost selected source");
        live.open_assembly(top.document_id)->session.commit(top);
        auto edited=live.open_part(f.part.document_id)->session.document();
        edited.sketches[1].edit_bspline_properties(f.curve,3,false,poles(.55));
        live.open_part(f.part.document_id)->session.commit(std::move(edited),f.bodies);
        workspace::regenerate_assembly(live,engine,top.document_id);
        std::cerr<<"Assembly curve case unsaved source regenerated\n";
        const auto& regenerated=live.open_assembly(top.document_id)->session.document().sketches.back();
        check(!regenerated.external_references[1].broken,"Unsaved authoritative source broke Assembly point");
        near(regenerated.external_references[1].cached_points.front()[0],.55);
        check(document::PartDocument::load(file).sketches[1].serialized()==f.part.sketches[1].serialized(),
            "Assembly regeneration saved or changed its read-only source file");
        static_cast<void>(live.remove(f.part.document_id));
        values=workspace::calculate_context_sketch_curve_intersections(live,top.document_id,target);
        check(values.size()==3&&std::ranges::none_of(values,[](const auto& r){return r.broken;}),"Closed native Part source lost curve intersection binding");
        const auto reopened=assembly::AssemblyDocument::load(top_file);
        values=workspace::calculate_assembly_sketch_intersections(engine,reopened,reopened.sketches.back());
        check(values.size()==3&&std::ranges::none_of(values,[](const auto& r){return r.broken;}),"Cold native Assembly lost exact curve points");
    }
    std::cout<<"Repeated/nested Assembly occurrences, closed native source and cold .asmz root binding refresh passed\n";
}
void actual_curve(const std::filesystem::path& file) {
    std::vector<kernel::BodyResult> geometry;auto part=document::PartDocument::load(file,&geometry);
    workspace::Workspace live;live.add_part(part,geometry);unsigned roots{};
    for(const auto& source:part.sketches)for(const auto& curve:source.bsplines)for(const auto& target:part.sketches)if(target.id!=source.id) {
        try {
            const auto values=workspace::prepare_sketch_curve_intersection_branches(live,part.document_id,target,{source.id,"bspline:"+curve.id,{}});
            if(values.empty())continue;
            std::cout<<"Actual Sketch spline "<<source.id<<" -> "<<target.id<<": "<<values.size()<<" isolated point(s)\n";
            for(const auto& r:values)check(r.curve_intersection&&r.kind==ExternalReferenceKind::Point,"Actual spline intersection is not a native associative Point");roots+=unsigned(values.size());
        } catch(const workspace::SketchOperationError&){}
    }
    check(roots>0,"Actual review model yielded no spline / later Sketch isolated point");
}
void query_timing(const kernel::OcctKernel& engine) {
    const auto f=fixture(engine);workspace::Workspace live;live.add_part(f.part,f.bodies);
    const auto before=live.open_part(f.part.document_id)->session.document().serialized();const auto revision=live.open_part(f.part.document_id)->session.revision();
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<100;++i) {
        const auto values=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,f.part.sketches.back(),{f.part.sketches[1].id,"bspline:"+f.curve,{}});
        check(values.size()==3,"Repeated explicit source query changed its result");
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    check(live.open_part(f.part.document_id)->session.document().serialized()==before&&live.open_part(f.part.document_id)->session.revision()==revision,
        "Repeated explicit query changed source data or Undo history");
    std::cout<<"100 explicit warm native source queries: "<<elapsed<<" ms total, "<<elapsed/100<<" ms/query; source/revision unchanged\n";
}
void persistence_validation(const kernel::OcctKernel& engine) {
    auto f=fixture(engine);workspace::Workspace live;live.add_part(f.part,f.bodies);
    auto target=f.part.sketches.back();const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,
        {f.part.sketches[1].id,"bspline:"+f.curve,{}});
    target.add_external_reference(branches[1]);const auto before=target.serialized();const auto native=nlohmann::json::parse(before);
    unsigned cases{};
    for(int variant=0;variant<8;++variant) {
        auto data=native;auto& reference=data["external_references"][0];auto& binding=reference["curve_intersection"];
        if(variant==0)reference.erase("curve_intersection");
        else if(variant==1)binding["version"]=2;
        else if(variant==2)binding["orientation"]=0;
        else if(variant==3)binding["root_count"]=0;
        else if(variant==4)binding["parameter_last"]=binding["parameter_first"];
        else if(variant==5)binding["branch_key"]="another-root";
        else if(variant==6)binding["curve_owner_id"]="another-source";
        else reference["kind"]="point";
        bool rejected=false;try{static_cast<void>(Sketch::from_serialized(data.dump()));}catch(const std::exception&){rejected=true;}
        check(rejected,"Malformed curve-point persistence was silently accepted");++cases;
    }
    check(target.serialized()==before,"Malformed readback test changed its source Sketch");
    std::cout<<cases<<" malformed curve-point payloads rejected without a legacy/default binding fallback\n";
}
void coincident_contact_branches(const kernel::OcctKernel& engine) {
    auto f=fixture(engine);auto control=poles(.5);for(auto& p:control)p[0]=0;
    f.part.sketches[1].edit_bspline_properties(f.curve,3,false,control);
    workspace::Workspace live;live.add_part(f.part,f.bodies);auto target=f.part.sketches.back();
    const auto branches=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,target,
        {f.part.sketches[1].id,"bspline:"+f.curve,{}});
    check(branches.size()==3,"Three coincident XYZ contacts lost distinct native branches");
    std::set<std::string> keys,points;
    for(const auto& r:branches) {
        near(r.cached_points.front()[0],0);near(r.cached_points.front()[1],0);
        check(keys.insert(r.curve_intersection->branch_key).second,"Equal-XYZ contacts share branch identity");
        target.add_external_reference(r);auto point=Sketch::create_point(0,0);const auto id=point.id;
        points.insert(id);target.points.push_back(std::move(point));static_cast<void>(target.add_point_reference_constraint(id,r.id));
    }
    solved(target);check(points.size()==3&&target.points.size()==3&&target.constraints.size()==3,"Equal-XYZ contours merged distinct native points or duplicated constraints");
    const auto cold=Sketch::from_serialized(target.serialized());check(cold.serialized()==target.serialized(),"Equal-XYZ point branches changed on native Sketch reopen");
    std::cout<<"Three equal-XYZ curve contacts retain distinct branch/native-point identities and exactly one contact each\n";
}
void bounded_sources(const kernel::OcctKernel& engine) {
    auto f=fixture(engine);const auto ids=f.part.sketches[1].retain_curve_intervals(f.curve,{{.35,.65}});
    check(ids.size()==1,"Trimmed spline fixture has no retained native interval");
    workspace::Workspace live;live.add_part(f.part,f.bodies);
    const auto roots=workspace::prepare_sketch_curve_intersection_branches(live,f.part.document_id,f.part.sketches.back(),
        {f.part.sketches[1].id,"bspline:"+ids.front(),{}});
    check(roots.size()==1,"Trimmed spline offered contacts on removed supporting intervals");near(roots.front().cached_points.front()[0],.5);
    auto rounded=fixture(engine);auto source=Sketch::create_default();source.id=rounded.part.sketches[1].id;source.owner_container_id=rounded.part.sketches[1].owner_container_id;
    const auto leg=source.add_segment(0,0,10,0),other=source.add_segment(0,0,0,10);static_cast<void>(source.add_corner_fillet(leg,other,2));const auto corner=source.corner_radii.front().id;
    rounded.part.sketches[1]=source;rounded.part.sketches.back().plane=SketchPlane::YZ;rounded.part.sketches.back().plane_offset=1;rounded.part.resolve_constructions();
    workspace::Workspace scene;scene.add_part(rounded.part,rounded.bodies);const auto& target=rounded.part.sketches.back();
    const auto absent=workspace::prepare_sketch_curve_intersection_branches(scene,rounded.part.document_id,target,{source.id,"segment:"+leg,{}});
    check(absent.empty(),"Rounded source leg offered a point on its removed design-vertex extension");
    const auto arc=workspace::prepare_sketch_curve_intersection_branches(scene,rounded.part.document_id,target,{source.id,"corner_radius:"+corner,{}});
    check(arc.size()==1,"Exact native rounded corner arc did not offer its bounded point");near(arc.front().cached_points.front()[0],2-std::sqrt(3.));near(arc.front().cached_points.front()[1],0);
    check(scene.open_part(rounded.part.document_id)->session.document().sketches[1].serialized()==rounded.part.sketches[1].serialized(),"Bounded source query modified its read-only Sketch");
    std::cout<<"Trimmed native spline, trimmed rounded support leg and native corner arc respect their actual bounded geometry\n";
}
}
int main(int argc,char** argv){try{const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());const auto directory=parent/("zima-curve-plane-"+Sketch::create_default().id);check(std::filesystem::create_directory(directory),"Cannot create private native test directory");
    const kernel::OcctKernel engine;bounded_sources(engine);part_cases(engine,directory);dimension_cases(engine);authored_3d(engine);analytic_sources(engine);assembly_cases(engine,directory);persistence_validation(engine);coincident_contact_branches(engine);if(argc>1)actual_curve(argv[1]);query_timing(engine);
    check(directory.parent_path()==parent,"Unsafe native fixture cleanup");std::filesystem::remove_all(directory);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
