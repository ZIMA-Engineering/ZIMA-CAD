#include <zima/sketcher/face_curve_reference.hpp>
#include "profile_command_fixture.hpp"
#include "broken_reference_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <iostream>
#include <stdexcept>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message+" "+args.dump());return result;
}
void verify_frame_reference_retirement() {
    using namespace sketcher;
    std::size_t cases=0;
    for(const auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle})
    for(bool driving:{false,true})for(bool locked:{false,true})for(bool full:{false,true})
    for(double tilt:{-30.,30.})for(double turn:{-30.,30.})for(double side:{-1.,1.})for(bool reverse:{false,true}) {
        auto part=document::PartDocument::create_default();
        auto source=document::PartDocument::create_construction(document::ConstructionKind::Point);source.origin={side*12,4,0};
        auto axis=document::PartDocument::create_construction(document::ConstructionKind::Axis);axis.origin=source.origin;axis.direction={0,0,1};
        auto sketch=Sketch::create_default();sketch.plane_auto=false;static_cast<void>(sketch.add_circle(0,0,10));
        const auto line=sketch.add_segment(side*12,4,side*12+14,12,1e-6,true,false,false);
        const auto first=sketch.segments.front().first_point_id,last=sketch.segments.front().second_point_id;
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=feature.id;
        part.history={feature};part.sketches={sketch};part.constructions={source,axis};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Frame references");
        graph.insert({document::PartHistoryKind::Construction,source.id});graph.insert({document::PartHistoryKind::Construction,axis.id});
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto geometry=part.construction_viewer_mesh().original_references;
        auto& owned=part.sketches.front();
        auto point=Sketch::create_external_reference(ExternalReferenceKind::Point);
        point.source_document_id=part.document_id;point.source_owner_id=source.container_origin.id;point.source_semantic_key="point";
        workspace::populate_external_reference_cache(owned,point,geometry);const auto point_id=point.id;owned.add_external_reference(point);
        auto support=Sketch::create_external_reference(ExternalReferenceKind::AxisPoint);
        support.source_document_id=part.document_id;support.source_owner_id=axis.entity_id;support.source_semantic_key="axis";
        workspace::populate_external_reference_cache(owned,support,geometry);const auto axis_id=support.id;owned.add_external_reference(support);
        static_cast<void>(owned.add_point_reference_constraint(first,point_id));
        const auto make_dimension=[&](DimensionKind k){return k==DimensionKind::Angle?
            owned.create_segment_dimension(line,k):owned.create_point_dimension(reverse?last:first,reverse?first:last,k);};
        auto dimension=make_dimension(kind);dimension.driving=driving;dimension.locked=locked;
        const auto dimension_id=dimension.id;
        if(!driving&&locked) {
            const auto before=owned.serialized();bool rejected=false;
            try{owned.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            require(rejected&&owned.serialized()==before,"Invalid locked reference dimension changed the Sketch");
            continue;
        }
        owned.apply_dimension(dimension);
        if(full) {
            const auto add=[&](DimensionKind k){auto d=make_dimension(k);d.locked=true;owned.apply_dimension(d);};
            if(!driving){add(DimensionKind::DistanceX);add(DimensionKind::DistanceY);}
            else add(kind==DimensionKind::DistanceX?DimensionKind::DistanceY:kind==DimensionKind::DistanceY?
                DimensionKind::DistanceX:kind==DimensionKind::Distance?DimensionKind::Angle:DimensionKind::Distance);
        }
        const auto original=owned.serialized();const auto immutable=part.constructions;
        workspace::Workspace live;live.add_part(part,{});const auto source_before=live.open_part(part.document_id)->session.document().serialized();
        auto edited=part;
        edited.history.front().placement.rotation_x=edited.history.front().placement.absolute_rotation_x=tilt;
        edited.history.front().placement.rotation_z=edited.history.front().placement.absolute_rotation_z=turn;
        edited.resolve_constructions(geometry);auto candidate=edited.sketches.front();
        if(cases==0) {
            auto retained=candidate;
            static_cast<void>(workspace::refresh_sketch_reference_snapshot(live,part.document_id,retained,false,body));
            require(std::ranges::any_of(retained.external_references,[&](const auto& r){return r.id==axis_id&&r.broken;}),
                "Ordinary source refresh erased an unavailable axis-point identity");
            workspace::Workspace unavailable;retained=candidate;
            static_cast<void>(workspace::refresh_sketch_reference_snapshot(unavailable,part.document_id,retained,true,body));
            require(std::ranges::any_of(retained.external_references,[&](const auto& r){return r.id==axis_id&&r.broken;}),
                "Frame refresh erased the identity of a missing native source");
            auto attached=part.sketches.front();const auto extra=attached.add_point(100,100);
            static_cast<void>(attached.add_point_reference_constraint(extra,axis_id));
            attached.resolved_origin=candidate.resolved_origin;attached.resolved_x_axis=candidate.resolved_x_axis;
            attached.resolved_y_axis=candidate.resolved_y_axis;attached.resolved_normal=candidate.resolved_normal;
            const auto authored_contact=attached.constraints.back();
            static_cast<void>(workspace::refresh_sketch_reference_snapshot(live,part.document_id,attached,true,body));
            require(std::ranges::any_of(attached.external_references,[&](const auto& r){return r.id==axis_id&&r.broken;})&&
                std::ranges::find(attached.constraints,authored_contact.id,&SketchConstraint::id)!=attached.constraints.end(),
                "Deliberate frame refresh removed a used axis point or its native contact");
            const auto restored=Sketch::from_serialized(attached.serialized());
            require(restored.external_references==attached.external_references&&restored.constraints==attached.constraints,
                "Broken used axis-point identity did not survive native reopen");
        }
        require(workspace::refresh_sketch_reference_snapshot(live,part.document_id,candidate,true,body),"Frame edit did not refresh its projected inputs");
        const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        const kernel::Vec3 delta{source.origin.x-candidate.resolved_origin.x,source.origin.y-candidate.resolved_origin.y,source.origin.z-candidate.resolved_origin.z};
        const double expected_x=dot(delta,candidate.resolved_x_axis),expected_y=dot(delta,candidate.resolved_y_axis);
        const auto equations=[&](const Sketch& s) {
            require(std::ranges::none_of(s.external_references,[&](const auto& r){return r.id==axis_id;}),"Unused tilted AxisPoint was retained");
            const auto ref=std::ranges::find(s.external_references,point_id,&SketchExternalReference::id);
            const auto* a=s.find_point(first);const auto* b=s.find_point(last);
            require(ref!=s.external_references.end()&&!ref->broken&&std::hypot(a->x-expected_x,a->y-expected_y)<1e-6,
                "Native contact did not follow the independent projected-point equation");
            for(const auto& d:s.dimensions) {
                const auto* p=s.find_point(d.first_point_id.empty()?first:d.first_point_id);
                const auto* q=s.find_point(d.second_point_id.empty()?last:d.second_point_id);
                const double actual=d.kind==DimensionKind::Distance?std::hypot(q->x-p->x,q->y-p->y):
                    d.kind==DimensionKind::DistanceX?q->x-p->x:d.kind==DimensionKind::DistanceY?q->y-p->y:
                    std::atan2(q->y-p->y,q->x-p->x)*180/std::numbers::pi;
                require(std::abs(actual-d.value)<1e-6,"Frame reference refresh changed a native dimension equation");
            }
        };
        equations(candidate);
        for(double increment:{.5,1.}) {
            const auto before=candidate.serialized();const auto value=std::ranges::find(candidate.dimensions,dimension_id,&SketchDimension::id)->value;
            bool accepted=false;try{accepted=candidate.set_dimension_value(dimension_id,value+increment);}catch(const std::exception&){}
            // Explicit numeric edits may change locked driving values; locks
            // constrain dragging. Reference setters are successful no-ops.
            require(accepted,"Frame-refreshed dimension rejected a feasible numeric edit");
            if(!driving)require(candidate.serialized()==before,"Reference value edit changed frame-refreshed geometry");
            equations(candidate);
        }
        for(int drag=0;drag<3;++drag) {
            const auto previous=*candidate.find_point(last);
            const bool fixed_by_locked_dimensions=full&&(!driving||locked);
            double x=previous.x+2,y=previous.y+1;
            const auto anchor=*candidate.find_point(first);const auto dimensions_before=candidate.dimensions;
            if(!fixed_by_locked_dimensions)for(const auto& d:candidate.dimensions)if(d.driving&&d.locked) {
                const double dx=previous.x-anchor.x,dy=previous.y-anchor.y;
                if(d.kind==DimensionKind::Distance) {
                    const double angle=std::atan2(dy,dx)+.05,radius=std::hypot(dx,dy);
                    x=anchor.x+radius*std::cos(angle);y=anchor.y+radius*std::sin(angle);
                } else if(d.kind==DimensionKind::DistanceX)x=previous.x;
                else if(d.kind==DimensionKind::DistanceY)y=previous.y;
                else {x=anchor.x+1.1*dx;y=anchor.y+1.1*dy;}
            }
            static_cast<void>(candidate.move_point(last,x,y));equations(candidate);
            for(const auto& d:dimensions_before)if(d.driving&&d.locked)
                require(std::ranges::find(candidate.dimensions,d.id,&SketchDimension::id)->value==d.value,
                    "Dragging changed a locked dimension value");
            const auto* moved=candidate.find_point(last);const double displacement=std::hypot(moved->x-previous.x,moved->y-previous.y);
            require(fixed_by_locked_dimensions?displacement<1e-6:displacement>1e-6,
                "Frame-refreshed native point has incorrect dragging mobility");
        }
        edited.sketches.front()=candidate;
        document::DocumentSession transaction(part,{});transaction.commit(edited,{});
        require(transaction.undo()&&transaction.document().sketches.front().serialized()==original,"Frame refresh Undo changed the original Sketch");
        require(transaction.redo(),"Frame refresh Redo missing");equations(transaction.document().sketches.front());
        const auto reopened=document::PartDocument::from_serialized(transaction.document().serialized());equations(reopened.sketches.front());
        require(reopened.constructions==immutable&&live.open_part(part.document_id)->session.document().serialized()==source_before,
            "Frame reference refresh modified its read-only source definitions");
        ++cases;
    }
    std::cout<<"Frame reference retirement: "<<cases<<" dimension/lock/mobility/orientation cases, repeated edits/drags, equations, native reopen and Undo/Redo passed\n";
}

void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    verify_frame_reference_retirement();
    for(bool reverse:{false,true})
    for(const auto kind:{sketcher::DimensionKind::Distance,sketcher::DimensionKind::DistanceX,
            sketcher::DimensionKind::DistanceY,sketcher::DimensionKind::Angle})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        auto sketch=test::broken_reference_fixture();
        if(reverse)std::swap(sketch.segments.front().first_point_id,sketch.segments.front().second_point_id);
        auto dimension=sketch.create_segment_dimension(sketch.segments.front().id,kind);
        dimension.driving=driving;dimension.locked=locked;
        const auto before=sketch.serialized();
        try{sketch.apply_dimension(dimension);}catch(const std::exception&){require(sketch.serialized()==before,"Rejected dimension changed a broken-reference Sketch");}
        const auto ids=sketch.external_references;const auto native_points=sketch.points;
        const auto native_segments=sketch.segments;const auto native_dimensions=sketch.dimensions;
        for(const auto& reference:ids) {
            const auto count=sketch.external_references.size();
            sketch.remove_geometry(reference.id);sketch.validate();
            require(sketch.external_references.size()+1==count,"Broken reference was not independently removed");
            require(sketch.points==native_points&&sketch.segments==native_segments&&sketch.dimensions==native_dimensions,
                "Removing a broken reference changed unrelated dimensioned geometry");
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());
        }
        require(sketch.solve().status!=sketcher::SolveStatus::Invalid,"Removing broken references did not leave an editable Sketch");
        const auto point=sketch.points.front().id;
        for(double delta:{1.,-2.,.25}) {
            const auto snapshot=sketch.serialized();const auto position=*sketch.find_point(point);
            if(!sketch.move_point(point,position.x+delta,position.y+delta))
                require(sketch.serialized()==snapshot,"Rejected drag changed geometry after broken-reference removal");
            require(sketch.solve().status!=sketcher::SolveStatus::Invalid,"Repeated drag invalidated the repaired Sketch");
        }
    }
    {
        auto document=document::PartDocument::create_default();
        auto source=sketcher::Sketch::create_default();static_cast<void>(source.add_circle(0,0,15));
        document.sketches.push_back(source);
        const auto geometry=workspace::sketch_external_reference_source_geometry(document,{});
        for(const auto& point:source.points)
            require(std::ranges::count_if(geometry.points,[&](const auto& p){return p.reference.owner_id==source.id&&p.reference.semantic_key=="point:"+point.id;})==1,"Standalone Sketch point missing or duplicated in regeneration sources");
        kernel::BodyResult calculated;calculated.mesh.original_references=geometry;
        const auto combined=workspace::sketch_external_reference_source_geometry(document,{calculated});
        require(combined.points.size()==geometry.points.size()&&combined.edges.size()==geometry.edges.size(),"Calculated Sketch sources were duplicated during reference refresh");
    }
    for(bool import_first:{false,true})for(bool reverse:{false,true}) {
        auto outline=sketcher::Sketch::create_default();
        auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
        reference.source_document_id="read-only-source";reference.source_owner_id="quarter-circle";reference.source_semantic_key="curve";
        reference.cached_points={{1,0},{0,1}};
        reference.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
        if(reverse) {
            std::ranges::reverse(reference.cached_points);std::ranges::reverse(reference.exact_spline->poles);
            kernel::reverse_bspline_parameters(reference.exact_spline->knots,reference.exact_spline->weights);
        }
        outline.add_external_reference(reference);
        if(import_first)static_cast<void>(outline.add_external_profile_geometry(reference.id));
        static_cast<void>(outline.add_segment(1,0,0,0,1e-9));static_cast<void>(outline.add_segment(0,0,0,1,1e-9));
        if(!import_first)static_cast<void>(outline.add_external_profile_geometry(reference.id));
        require(outline.points.size()==3&&outline.arcs.size()==1&&outline.bsplines.empty(),"Circular import did not share its actual centre and boundary endpoints");
        const auto profile=outline.arcs.front();const auto linked=outline.serialized();
        require(!outline.move_point(profile.start_point_id,2,2)&&outline.serialized()==linked,
            "Shared exact endpoint escaped its read-only source");
        auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_extrusion_container(outline.id);
        outline.owner_container_id=feature.id;feature.extrusion.sketch_id=outline.id;feature.extrusion.length_forward=2;feature.extrusion.height=2;
        part.history={feature};part.sketches={outline};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Exact closed boundary"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
        const auto result=kernel.evaluate_history(part.kernel_operations());
        require(!result.empty()&&result.back().calculation_errors.empty()&&std::abs(result.back().volume-std::acos(-1.)/2)<1e-7,
            "Exact imported boundary cannot create its independently checked quarter-cylinder volume");
        const auto roundtrip=sketcher::Sketch::from_serialized(outline.serialized());
        require(roundtrip.arcs.front()==profile&&roundtrip.import_blocks.front().point_ids==outline.import_blocks.front().point_ids,
            "Native reopen changed circular profile identity or source endpoint mapping");
    }
    // Independent world-to-local check: moving the destination frame must
    // reproject the fixed source, including geometry linked to its endpoints.
    for(const double angle:{0.0,30.0,90.0,180.0,-90.0,270.0})
    for(const double shift:{0.0,17.0}) {
        auto sketch=sketcher::Sketch::create_default();
        sketch.owner_container_id="destination-container";
        kernel::ViewerReferenceGeometry source;
        kernel::ViewerEdge edge;edge.reference={"fixed-source","edge",{}};
        edge.points={{12,4,0},{24,4,0}};source.edges.push_back(edge);
        auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
        reference.source_document_id="fixed-document";reference.source_owner_id="fixed-source";
        reference.source_semantic_key="edge";reference.cached_points={{12,4},{24,4}};
        const auto id=reference.id;sketch.add_external_reference(reference);
        static_cast<void>(sketch.add_external_profile_geometry(id));
        const double radians=angle*std::acos(-1.0)/180.0,c=std::cos(radians),s=std::sin(radians);
        sketch.resolved_origin={shift,-shift,0};sketch.resolved_x_axis={c,s,0};
        sketch.resolved_y_axis={-s,c,0};sketch.resolved_normal={0,0,1};
        static_cast<void>(sketch.refresh_external_references("fixed-document",source));
        const auto found=std::ranges::find(sketch.external_references,id,&sketcher::SketchExternalReference::id);
        require(found!=sketch.external_references.end()&&!found->broken,"Destination frame broke a valid external edge");
        for(std::size_t i=0;i<2;++i) {
            const double dx=edge.points[i].x-shift,dy=edge.points[i].y+shift;
            const auto expected=std::array{c*dx+s*dy,-s*dx+c*dy};
            require(std::hypot(found->cached_points[i][0]-expected[0],found->cached_points[i][1]-expected[1])<1e-8,
                "External edge followed the destination Origin instead of its source");
            const auto& segment=sketch.segments.front();
            const auto* endpoint=sketch.find_point(i==0?segment.first_point_id:segment.second_point_id);
            require(endpoint&&std::hypot(endpoint->x-expected[0],endpoint->y-expected[1])<1e-8,
                "Linked profile endpoint retained its previous local coordinates");
        }
        const auto stable=sketch.serialized();
        require(!sketch.refresh_external_references("fixed-document",source)&&sketch.serialized()==stable,
            "Repeated external refresh drifted a settled projection");
        auto reopened=sketcher::Sketch::from_serialized(stable);
        require(!reopened.refresh_external_references("fixed-document",source),"Reopened external projection was stale");
        auto jittered=reopened;auto numerical_noise=source;
        for(int step=0;step<4;++step)numerical_noise.edges.front().points.back().x=
            std::nextafter(numerical_noise.edges.front().points.back().x,
                std::numeric_limits<double>::infinity());
        const auto jittered_stable=jittered.serialized();
        require(!jittered.refresh_external_references("fixed-document",numerical_noise)&&
            jittered.serialized()==jittered_stable,
            "Floating-point reprojection noise invalidated an unchanged external reference cache");
        numerical_noise.edges.front().points.back().x+=1e-8;
        require(jittered.refresh_external_references("fixed-document",numerical_noise),
            "A real sub-micron external source movement was hidden as numerical noise");
        for(int constraints=0;constraints<3;++constraints) {
            auto part=document::PartDocument::create_default();part.body_history={};
            auto source_sketch=sketcher::Sketch::create_default();
            auto root=document::PartDocument::create_extrusion_container(source_sketch.id);
            source_sketch.owner_container_id=root.id;
            auto child=document::PartDocument::create_extrusion_container(sketch.id);
            auto owned=sketch;owned.owner_container_id=child.id;
            for(auto& r:owned.external_references) {
                r.source_document_id=part.document_id;r.source_owner_id=root.id;
            }
            child.placement.x=shift;child.placement.y=-shift;
            child.placement.rotation_z=child.placement.absolute_rotation_z=angle;
            auto geometry=source;geometry.edges.front().reference.owner_id=root.id;
            geometry.points.push_back({{shift,-shift,0},{root.id,"anchor",{}}});
            geometry.vertices={{0,0,0},{1,0,0},{0,1,0}};geometry.triangles={0,1,2};
            geometry.triangle_references.push_back({root.id,"plane",{}});
            if(constraints==1)child.placement.references.push_back({{},root.id,"plane"});
            if(constraints==2)child.placement.references.push_back({{},root.id,"anchor"});
            part.history={root,child};part.sketches={source_sketch,owned};
            part.history_order={{document::PartHistoryKind::Feature,root.id},{document::PartHistoryKind::Feature,child.id}};
            part.resolve_constructions(geometry);
            kernel::BodyResult calculated;calculated.mesh.original_references=geometry;
            static_cast<void>(workspace::refresh_sketch_external_references(part,{calculated}));
            require(part.reference_errors.empty(),"Container placement left a conflicting external projection");
            const auto& actual=part.sketches.back();
            const auto projected=std::ranges::find(actual.external_references,id,&sketcher::SketchExternalReference::id);
            require(projected!=actual.external_references.end()&&!projected->broken,"Container placement lost its external source");
            for(std::size_t i=0;i<2;++i) {
                const double dx=edge.points[i].x-shift,dy=edge.points[i].y+shift;
                require(std::hypot(projected->cached_points[i][0]-(c*dx+s*dy),
                    projected->cached_points[i][1]-(-s*dx+c*dy))<1e-8,
                    "Free, plane-constrained or point-constrained container used a stale external projection");
            }
        }
    }
    // Unusable projections retain their identity and last native geometry,
    // including during deliberate destination-frame edits.
    for (const bool axis_point : {false,true}) {
        auto sketch=sketcher::Sketch::create_default();
        kernel::ViewerReferenceGeometry source;
        source.edges.push_back({{{0,0,0},{0,0,10}},{"source","edge",{}}});
        source.axes.push_back({{0,0,0},{1,0,0},100,{"source","axis",{}}});
        auto ref=sketcher::Sketch::create_external_reference(axis_point?sketcher::ExternalReferenceKind::AxisPoint:sketcher::ExternalReferenceKind::Edge);
        ref.source_document_id="source-document";ref.source_owner_id="source";
        ref.source_semantic_key=axis_point?"axis":"edge";ref.cached_points={{0,0}};
        if(!axis_point)ref.cached_points.push_back({10,0});
        sketch.add_external_reference(ref);
        const auto own=sketch.add_segment(0,0,10,10);
        auto ordinary=sketch;
        static_cast<void>(ordinary.refresh_external_references("source-document",source));
        require(ordinary.external_references.size()==1&&ordinary.external_references.front().broken,"Ordinary refresh erased an unavailable identity");
        static_cast<void>(sketch.refresh_external_references("source-document",source,false,nullptr,true));
        require(sketch.external_references.size()==1&&sketch.external_references.front().broken&&sketch.segments.front().id==own,"Frame edit detached a collapsed projection or removed native geometry");
    }
    for (const bool fixed : {false,true}) {
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id="destination-container";
        kernel::ViewerReferenceGeometry source;
        source.points.push_back({{12,4,0},{"source","point",{}}});
        source.points.push_back({{20,5,0},{"source","other",{}}});
        auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
        ref.source_document_id="source-document";ref.source_owner_id="source";ref.source_semantic_key="point";ref.cached_points={{12,4}};
        sketch.add_external_reference(ref);
        auto other=ref;other.id=sketcher::Sketch::create_external_reference(ref.kind).id;other.source_semantic_key="other";other.cached_points={{20,5}};sketch.add_external_reference(other);
        const auto line=sketch.add_segment(12,4,30,12);
        const auto point=sketch.segments.front().first_point_id;
        require(std::ranges::any_of(sketch.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference&&c.first_point_id==point&&c.second_point_id==ref.id;}),"Native frame fixture did not anchor its endpoint");
        if(fixed)sketch.set_point_fixed(point,true);
        sketch.resolved_origin={17,-3,0};
        const auto sources=source.points;
        static_cast<void>(sketch.refresh_external_references("source-document",source,false,nullptr,true));
        const auto kept=std::ranges::find(sketch.external_references,other.id,&sketcher::SketchExternalReference::id);
        require(kept!=sketch.external_references.end()&&kept->cached_points.front()==std::array{3.,8.},"Frame edit lost an unrelated valid projection");
        if(fixed) {
            require(std::ranges::any_of(sketch.external_references,[&](const auto& r){return r.id==ref.id&&r.broken;}),"Impossible fixed-point projection was auto-detached");
            require(std::ranges::any_of(sketch.constraints,[&](const auto& c){return c.first_point_id==ref.id||c.second_point_id==ref.id;}),"Unresolved projection discarded its authored point relation");
        }else require(std::hypot(sketch.find_point(point)->x+5,sketch.find_point(point)->y-7)<1e-8,"Frame reprojection did not move the dependent point");
        require(source.points.size()==sources.size()&&std::ranges::equal(source.points,sources,[](const auto& a,const auto& b){return a.position==b.position&&a.reference==b.reference;}),"Frame edit modified its immutable source");
        const auto stored=sketch.serialized();
        auto reopened=sketcher::Sketch::from_serialized(stored);
        require(!reopened.refresh_external_references("source-document",source,false,nullptr,true),"Reopened frame projection was stale");
    }
    {
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id="destination-container";
        kernel::ViewerReferenceGeometry source;
        std::vector<std::string> ids;
        for(const auto& position:{std::array{12.,4.},std::array{20.,5.}}) {
            auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
            ref.source_document_id="source-document";ref.source_owner_id="source";ref.source_semantic_key=std::to_string(ids.size());ref.cached_points={position};
            source.points.push_back({{position[0],position[1],0},{"source",ref.source_semantic_key,{}}});
            ids.push_back(ref.id);sketch.add_external_reference(ref);
        }
        const auto line=sketch.add_segment(12,4,20,5);
        require(sketch.constraints.size()==2,"Multiple frame fixture did not bind both external endpoints");
        sketch.set_point_fixed(sketch.segments.front().first_point_id,true);
        sketch.set_point_fixed(sketch.segments.front().second_point_id,true);
        sketch.resolved_origin={17,-3,0};
        static_cast<void>(sketch.refresh_external_references("source-document",source,false,nullptr,true));
        require(sketch.external_references.size()==2&&std::ranges::all_of(sketch.external_references,[](const auto& r){return r.broken;})&&sketch.segments.front().id==line,"Multiple incompatible projections lost their unresolved identities or native geometry");
        require(sketch.solve().status!=sketcher::SolveStatus::Conflicting,"Frozen reference geometry left conflicting equations");
    }
    {
        {
        workspace::Workspace curves;command_host::Options curve_options;
        curve_options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
        command_host::Host curve_host(curves,kernel,directory,curve_options);
        run(curve_host,"new",{{"type","part"},{"name","individual-outlines"}});
        const auto cylinder=zima::test::circular_commands([&](const char* n,Json a){return run(curve_host,n,std::move(a));},{{"radius_mm",5},{"height_mm",30}}).data.at("container").get<std::string>();
        const auto doc=curves.active_document_id();
        const auto refs=curves.open_part(doc)->session.calculated_boundaries().back().mesh.original_references;
        const auto face=std::ranges::find_if(refs.triangle_references,[&](const auto& r){return r.owner_id==cylinder&&r.surface&&r.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;});
        require(face!=refs.triangle_references.end(),"Individual outline fixture cylinder missing");
        const auto sketch=run(curve_host,"sketch.create",{{"plane","XZ"},{"name","Individual generators"}}).data.at("sketch").get<std::string>();
        const auto current=[&]{return workspace::document_sketch(curves,doc,sketch);};
        for(int side:{-1,1})run(curve_host,"sketch.reference.create",{{"sketch",sketch},{"kind","face"},{"owner",cylinder},{"key",sketcher::face_curve_key(face->semantic_key,side)}});
        require(current().external_references.size()==2,"Opposite outline identities were merged");
        for(const auto& ref:current().external_references)require(!ref.broken&&!ref.infinite&&ref.cached_paths.size()==1&&ref.cached_paths.front().size()==2,"Individual outline contains the whole source face");
        run(curve_host,"undo");require(current().external_references.size()==1,"Outline creation Undo failed");
        run(curve_host,"redo");require(current().external_references.size()==2,"Outline creation Redo failed");
        const auto original=current().external_references.front();
        const auto point=run(curve_host,"sketch.point.create",{{"sketch",sketch},{"position",{5,-6}}}).data.at("point");
        run(curve_host,"sketch.constraint.create",{{"sketch",sketch},{"kind","point_on_line"},{"points",{point}},{"geometry",{original.id}}});
        const auto contacts=current().constraints;
        const auto stable=current().serialized();
        require(!curve_host.execute({{"command","sketch.reference.replace"},{"arguments",{{"sketch",sketch},{"reference",original.id},
            {"kind","face"},{"owner",cylinder},{"key","missing"}}}}).ok&&current().serialized()==stable,
            "Invalid reference replacement changed native geometry or contacts");
        run(curve_host,"sketch.reference.replace",{{"sketch",sketch},{"reference",original.id},{"kind","face"},{"owner",cylinder},{"key",face->semantic_key}});
        run(curve_host,"sketch.reference.replace",{{"sketch",sketch},{"reference",original.id},{"kind","face"},{"owner",cylinder},{"key",original.source_semantic_key}});
        require(current().constraints==contacts&&current().external_references.front()==original,"Reference replacement lost contacts or selected side");
        run(curve_host,"undo");run(curve_host,"redo");
        require(current().constraints==contacts&&current().external_references.front()==original,"Reference replacement Undo/Redo lost identity");
        const auto identities=current().external_references;
        for(double offset:{100.,-100.,0.}) {
            run(curve_host,"sketch.set",{{"sketch",sketch},{"plane_offset_mm",offset}});
            run(curve_host,"regenerate");
            const auto refreshed=current();
            for(std::size_t i=0;i<2;++i)require(refreshed.external_references[i]==identities[i],"Normal frame displacement or regeneration changed an individual outline identity/cache");
        }
        unsigned profile_cases=0;
        for(const auto& support:identities)for(bool reversed:{false,true}) {
            auto linked=current();const auto source_before=curves.open_part(doc)->session.document().serialized();
            const auto segment_id=linked.add_external_profile_geometry(support.id);
            require(linked.segments.size()==1&&linked.bsplines.empty(),"A finite generator did not create exactly one native Segment");
            if(reversed)std::swap(linked.segments.front().first_point_id,linked.segments.front().second_point_id);
            const auto segment=linked.segments.front();
            const auto equations=[&](const auto& value,double height) {
                const auto* a=value.find_point(segment.first_point_id);const auto* b=value.find_point(segment.second_point_id);
                require(a&&b&&std::abs(a->x-b->x)<1e-8&&std::abs(std::hypot(a->x-b->x,a->y-b->y)-height)<1e-7,
                    "A projected generator escaped its exact finite endpoint equations");
                require(std::abs(std::abs(a->x)-5)<1e-7&&std::abs(std::abs(b->x)-5)<1e-7,"A generator changed its physical contour side");
            };
            equations(linked,30);
            for(const auto kind:{sketcher::DimensionKind::Distance,sketcher::DimensionKind::DistanceX,sketcher::DimensionKind::DistanceY,sketcher::DimensionKind::Angle})
            for(bool driving:{false,true})for(bool locked:{false,true}) {
                auto measured=linked;auto dimension=measured.create_segment_dimension(segment_id,kind);
                dimension.driving=driving;dimension.locked=locked;const auto before=measured.serialized();bool accepted=false;
                try{measured.apply_dimension(dimension);accepted=true;}
                catch(const std::exception&){require(measured.serialized()==before,"Rejected generator dimension mutated its geometry");}
                if(accepted) {
                    equations(measured,30);
                    for(double delta:{1.,-2.,.5}) {
                        const auto stable=measured.serialized();
                        const bool applied=measured.set_dimension_value(dimension.id,dimension.value+delta);
                        require((!driving||!applied)&&measured.serialized()==stable,
                            "An impossible dimension changed a read-only finite generator");
                        equations(measured,30);
                    }
                    for(const auto& point:{segment.first_point_id,segment.second_point_id})for(double delta:{1.,-3.,.25}) {
                        const auto stable=measured.serialized();const auto* position=measured.find_point(point);
                        require(!measured.move_point(point,position->x+delta,position->y+delta)&&measured.serialized()==stable,
                            "Dragging escaped a generator endpoint reference");
                    }
                    auto reopened=sketcher::Sketch::from_serialized(measured.serialized());equations(reopened,30);
                }
                ++profile_cases;
            }
            for(double height:{35.,20.,32.,30.}) {
                auto changed=curves.open_part(doc)->session.document();
                changed.find_container(cylinder)->feature.sides[0].length=height;changed.resolve_constructions();
                const auto source_geometry=kernel.evaluate_history(changed.kernel_operations());
                const auto geometry=workspace::sketch_external_reference_source_geometry(changed,source_geometry);
                static_cast<void>(linked.refresh_external_references(doc,geometry));
                equations(linked,height);
                for(const auto& reference:linked.external_references)require(!reference.broken,"Source edit broke a generator or its endpoint reference");
                linked=sketcher::Sketch::from_serialized(linked.serialized());equations(linked,height);++profile_cases;
            }
            auto candidate=curves.open_part(doc)->session.document();
            *std::ranges::find(candidate.sketches,sketch,&sketcher::Sketch::id)=linked;
            document::DocumentSession session(curves.open_part(doc)->session.document(),curves.open_part(doc)->session.calculated_boundaries());
            const auto before=session.document().serialized();session.commit(candidate,session.calculated_boundaries());
            require(session.undo()&&session.document().serialized()==before,"Generator profile Undo lost its source definition");
            require(session.redo()&&session.document().sketches.back().serialized()==linked.serialized(),"Generator profile Redo lost endpoint identity");
            require(curves.open_part(doc)->session.document().serialized()==source_before,"Generator matrix changed its read-only source");
        }
        std::cout<<"Finite outline profiles: "<<profile_cases<<" length/X/Y/angle, driving/reference, locked/unlocked, endpoint orders, repeated rejected edits/drags, source height changes, native roundtrip and Undo/Redo checks passed\n";
        const auto file=directory/"individual-outlines-roundtrip.prtz";run(curve_host,"save_as",{{"path",file.string()}});
        const auto reopened=document::PartDocument::load(file);
        auto persisted=identities;for(auto& reference:persisted)reference.source_document_id=reopened.document_id;
        require(reopened.sketches.back().external_references==persisted,"Individual outlines failed native save/reopen");
    }
    workspace::Workspace live;command_host::Options options;
        options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
        command_host::Host host(live,kernel,directory,options);
        run(host,"new",{{"type","part"},{"name","body-edge-projection"}});
        const auto doc=live.active_document_id();
        const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","40"},{"width_mm","30"},{"height_mm","20"}}).data.at("container").get<std::string>();
        const Json seed={{"owner",box},{"key",test::profile_key(live.open_part(doc)->session.document(),box,"edge:x_max:y_min:z_max--x_max:y_min:z_min")}};
        const auto chamfer=run(host,"chamfer.create",{{"routes",Json::array({Json{{"edges",Json::array({seed})}}})},{"distance_a_mm",2}}).data.at("container").get<std::string>();
        auto* state=live.open_part(doc);
        const auto& mesh=state->session.calculated_boundaries().back().mesh;
        const auto changed=std::ranges::find_if(mesh.edges,[&](const auto& e) {
            if(e.reference.owner_id!=box||e.points.size()<2||std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)<1)return false;
            const auto source=std::ranges::find_if(mesh.original_references.edges,[&](const auto& r){return r.reference==e.reference;});
            return source!=mesh.original_references.edges.end()&&source->points!=e.points;
        });
        require(changed!=mesh.edges.end(),"Chamfer fixture lacks a trimmed edge with retained source identity");
        const auto key=changed->reference.semantic_key;
        const auto generated=std::ranges::find_if(mesh.edges,[&](const auto& e){return e.reference.owner_id==chamfer&&e.points.size()>=2&&std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)>1;});
        require(generated!=mesh.edges.end(),"Chamfer fixture lacks a generated body edge");
        const auto generated_key=generated->reference.semantic_key;
        const auto sketch=run(host,"sketch.create",{{"name","Body projection"}}).data.at("sketch").get<std::string>();
        const auto command=[&](const char* name,Json args){args["sketch"]=sketch;return run(host,name,args).data;};
        const auto original=command("sketch.reference.create",{{"kind","edge"},{"owner",box},{"key",key}}).at("reference").get<std::string>();
        const auto projected=command("sketch.reference.create",{{"kind","edge"},{"owner",box},{"key",key},{"profile",true}}).at("reference").get<std::string>();
        const auto reference=[&](const auto& s,const std::string& id)->const sketcher::SketchExternalReference& {
            const auto r=std::ranges::find(s.external_references,id,&sketcher::SketchExternalReference::id);require(r!=s.external_references.end(),"Projection reference missing");return *r;
        };
        auto current=workspace::document_sketch(live,doc,sketch);
        const auto original_points=reference(current,original).cached_points;
        const auto body_points=reference(current,projected).cached_points;
        require(!reference(current,original).body_edge&&reference(current,projected).body_edge&&original_points!=body_points,
            "External Geometry used the untrimmed original edge");
        require(std::ranges::all_of(current.external_references,[&](const auto& r){return r.id==original||r.body_edge;}),"Projected endpoint lost its body-edge source");
        run(host,"save");
        const auto reopened=document::PartDocument::load(directory/"body-edge-projection.prtz");
        require(reference(reopened.sketches.back(),projected)==reference(current,projected),"Body edge mode did not survive native save/reopen");
        const auto generated_sketch=run(host,"sketch.create",{{"name","Chamfer edge"}}).data.at("sketch").get<std::string>();
        const auto generated_reference=run(host,"sketch.reference.create",{{"sketch",generated_sketch},{"kind","edge"},{"owner",chamfer},{"key",generated_key},{"profile",true}}).data.at("reference").get<std::string>();
        run(host,"chamfer.set",{{"container",chamfer},{"distance_a_mm",4}});
        current=workspace::document_sketch(live,doc,sketch);
        require(!reference(current,projected).broken&&reference(current,projected).cached_points!=body_points&&reference(current,original).cached_points==original_points,
            "Regeneration confused original references with body-edge geometry");
        const auto generated_result=workspace::document_sketch(live,doc,generated_sketch);
        require(reference(generated_result,generated_reference).body_edge&&!reference(generated_result,generated_reference).broken&&reference(generated_result,generated_reference).source_semantic_key==generated_key,
            "Generated Chamfer edge identity did not survive regeneration");
        run(host,"undo");current=workspace::document_sketch(live,doc,sketch);
        require(reference(current,projected).cached_points==body_points,"Undo lost the projected body edge");
        run(host,"redo");current=workspace::document_sketch(live,doc,sketch);
        require(reference(current,projected).body_edge&&!reference(current,projected).broken,"Redo lost the body-edge source mode");
    }
    {
        auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{30,30,30});part.history.push_back(box);
        auto thread=document::PartDocument::create_thread_container();thread.placement.z=-15;thread.thread.bore_length=20;thread.thread.length_forward=15;part.history.push_back(thread);
        const auto calculated=kernel.evaluate_history(part.kernel_operations());
        auto source=calculated.back().mesh.original_references;
        const auto axis=std::ranges::find_if(source.axes,[&](const auto& a){return a.reference.owner_id==thread.id&&a.reference.semantic_key=="axis:primary";});
        require(axis!=source.axes.end(),"Thread did not persist its source axis");
        auto sketch=sketcher::Sketch::create_default();
        auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Axis);
        reference.source_document_id=part.document_id;reference.source_owner_id=thread.id;reference.source_semantic_key="axis:primary";
        workspace::populate_external_reference_cache(sketch,reference,source);
        require(reference.kind==sketcher::ExternalReferenceKind::AxisPoint&&!reference.infinite&&reference.cached_points.size()==1,"Perpendicular thread axis did not become a point");
        const auto id=reference.id;sketch.add_external_reference(reference);
        const auto line=sketch.add_segment(0,0,10,0);
        static_cast<void>(sketch.add_external_point_segment_constraint(id,line,false));
        const auto binding=std::ranges::find_if(sketch.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference;});
        require(binding!=sketch.constraints.end(),"External line contact lost its point binding");
        const auto markers=sketch.viewer_mesh().constraint_markers;
        require(std::ranges::any_of(markers,[&](const auto& m){return m.reference.semantic_key=="constraint:"+binding->id&&m.label=="C";}),
            "External axis point binding does not display C");
        const auto constraints=sketch.constraints;
        // Tilt the source relative to the Sketch; camera changes are absent
        // from this persisted geometry contract.
        axis->direction={0.1,0,std::sqrt(.99)};
        require(sketch.refresh_external_references(part.document_id,source),"Axis tilt did not invalidate the point");
        require(sketch.external_references.front().broken&&sketch.constraints==constraints&&sketch.solve().status==sketcher::SolveStatus::Invalid,"Broken axis point lost constraints or silently solved");
        const auto hidden=sketch.viewer_mesh();
        require(std::ranges::none_of(hidden.points,[&](const auto& p){return p.reference.semantic_key.starts_with("external_point:"+id);}),"Broken axis point remains visible/pickable");
        sketch=sketcher::Sketch::from_serialized(sketch.serialized());
        require(sketch.external_references.front().kind==sketcher::ExternalReferenceKind::AxisPoint&&sketch.constraints==constraints,"Save/reopen changed point identity or constraints");
        axis->direction={0,0,-1};axis->point.x+=2;
        require(sketch.refresh_external_references(part.document_id,source)&&!sketch.external_references.front().broken,"Restoring perpendicularity did not repair the reference");
        require(sketch.external_references.front().id==id&&sketch.constraints==constraints&&sketch.solve().status!=sketcher::SolveStatus::Invalid,"Repaired point did not retain its constraints");
        require(sketch.external_references.front().cached_points.front()[0]==2,"Restored point did not follow the moved axis");
        sketch.plane_reference_owner_id="test-plane";
        sketch.resolved_x_axis={1,0,0};sketch.resolved_y_axis={0,0,1};sketch.resolved_normal={0,-1,0};
        require(sketch.refresh_external_references({},source,true)&&sketch.external_references.front().broken,
            "Tilting the Sketch frame did not invalidate the axis point");
        sketch.plane_reference_owner_id.clear();sketch.refresh_default_frame();
        require(sketch.refresh_external_references({},source,true)&&!sketch.external_references.front().broken&&sketch.constraints==constraints,
            "Restoring the Sketch frame did not recover its point and constraints");
        source.axes.clear();sketch.refresh_external_references(part.document_id,source);
        require(sketch.external_references.front().broken&&sketch.constraints==constraints,"Deleted source erased its dependent constraints");
        sketch.remove_geometry(id);
        require(sketch.external_references.empty()&&std::ranges::none_of(sketch.constraints,[](const auto& constraint){
                return constraint.kind==sketcher::ConstraintKind::PointReference;})&&sketch.segments.front().id==line,
            "Removing a broken axis point did not remove only its dependent contact");
        require(sketch.solve().status!=sketcher::SolveStatus::Invalid,"A removed broken contact left the Sketch invalid");
    }
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","references"}});const auto doc=live.active_document_id();
    const auto box=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto sketch=run(host,"sketch.create",{{"name","Projection"}}).data.at("sketch").get<std::string>();
    auto* state=live.open_part(doc);const auto current=[&]{return workspace::document_sketch(live,doc,sketch);};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto source=state->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=*std::ranges::find_if(source.edges,[&](const auto& e){return e.reference.owner_id==box && e.points.size()>=2 && std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)>1;});
    Json edge_args={{"kind","edge"},{"owner",box},{"key",edge.reference.semantic_key}};
    const auto cache=state->session.calculated_boundaries().back().volume;
    const auto ref=command("sketch.reference.create",edge_args).at("reference").get<std::string>();
    require(current().external_references.front().source_document_id==doc && state->session.calculated_boundaries().back().volume==cache,"Projection changed source cache or document identity");
    const auto own=command("sketch.reference.project",{{"reference",ref}}).at("geometry").get<std::string>();
    require(current().segments.size()==1 && current().import_blocks.empty() && current().external_references.size()==3 && current().constraints.size()==4,"Projected line did not retain its source link");
    for(int missing=0;missing<3;++missing) {
        auto invalid=current();auto& r=invalid.external_references.front();r.context_assembly_document_id="context";r.context_instance_path="dependent";r.source_instance_path="source";
        if(missing==0)r.source_instance_path.clear();if(missing==1)r.context_assembly_document_id.clear();if(missing==2)r.context_instance_path.clear();
        bool rejected=false;try{invalid.validate();}catch(const std::exception&){rejected=true;}require(rejected,"Incomplete in-context Part reference accepted");
    }
    const auto stable=current().serialized();const auto revision=state->session.revision();
    auto duplicate=edge_args;duplicate["sketch"]=sketch;
    require(!host.execute({{"command","sketch.reference.create"},{"arguments",duplicate}}).ok && current().serialized()==stable && state->session.revision()==revision,"Duplicate source partially committed");
    require(!host.execute({{"command","sketch.reference.project"},{"arguments",{{"sketch",sketch},{"reference",ref}}}}).ok,"Source produced duplicate native geometry");
    const auto points=current().points;command("sketch.reference.delete",{{"reference",ref}});
    require(current().external_references.empty() && current().points==points && current().segments.front().id==own,"Detach destroyed projected geometry");run(host,"undo");require(current().external_references.size()==3,"Detach Undo did not restore its dependency");
    const auto face=*std::ranges::find_if(source.triangle_references,[&](const auto& f){return f.owner_id==box && f.surface && std::abs(f.surface->axis.x)>.9;});
    command("sketch.reference.create",{{"kind","face"},{"owner",box},{"key",face.semantic_key}});
    const auto face_projection=current().external_references.back();
    require(face_projection.infinite && face_projection.cached_points.size()==2 &&
        face_projection.cached_paths.empty() && face_projection.source_owner_id==box &&
        face_projection.source_semantic_key==face.semantic_key,
        "Edge-on planar face did not retain its projected line support and source identity");
    const auto source_point=*std::ranges::find_if(source.points,[&](const auto& p){return p.reference.owner_id==box;});
    command("sketch.reference.create",{{"kind","point"},{"owner",box},{"key",source_point.reference.semantic_key}});
    require(current().external_references.back().cached_points.front()==current().local_point(source_point.position),"Point projection used wrong frame");
    const auto origin=state->session.document().find_container(box)->container_origin.id;
    const auto axes=run(host,"reference.list",{{"kind","axis"},{"owner",origin}}).data.at("items");
    bool axis_created=false;for(const auto& axis:axes) {
        const auto data=run(host,"reference.get",axis).data;
        if(std::abs(data.at("direction")[2].get<double>())>.9)continue;
        command("sketch.reference.create",axis);axis_created=true;break;
    }
    require(axis_created && current().external_references.back().infinite,"Original history axis could not be projected");
    auto bad=edge_args;bad["sketch"]=sketch;bad["key"]="result-only";
    require(!host.execute({{"command","sketch.reference.create"},{"arguments",bad}}).ok,"Missing source was guessed");
    const auto later=zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}}).data.at("container");
    bad["owner"]=later;require(host.execute({{"command","sketch.reference.create"},{"arguments",bad}}).code=="invalid_reference_source","Forward dependency accepted");
    // Dialog-owned drafts use the active Body insertion boundary without
    // inserting a temporary feature or widening the stored Sketch contract.
    {
        const auto original=state->session.document();
        const auto calculated=state->session.calculated_boundaries();
        const auto body=original.body_history.active_body_id();
        require(!body.empty(),"Reference fixture has no active Body");
        auto draft=sketcher::Sketch::create_default();
        draft.owner_container_id=document::PartDocument::create_sketch_container().id;
        auto context=original;
        context.body_history.set_history_cursor(body,1);
        state->session.commit(context,calculated);
        require(workspace::sketch_external_reference_source_owners(context,draft.id).empty(),
                "Unknown Sketch identity acquired an implicit Body");
        const auto section_sources=workspace::sketch_external_reference_source_owners(context,draft.id,{},true);
        require(section_sources.contains(box)&&section_sources.contains(later.get<std::string>()),
                "New Section Sketch cannot reference the complete model");
        const auto section_reference=workspace::prepare_sketch_external_reference(live,doc,draft,
            sketcher::ExternalReferenceKind::Edge,box,edge.reference.semantic_key,{}, {},true);
        require(!section_reference.broken&&section_reference.cached_points.size()>=2&&
                state->session.document().sections.empty(),"Section draft projection modified the document or lost its source");
        const auto allowed=workspace::sketch_external_reference_source_owners(context,draft.id,body);
        require(allowed.contains(box)&&!allowed.contains(later.get<std::string>()),
                "Draft references ignored the Body insertion cursor");
        const auto prepared=workspace::prepare_sketch_external_reference(live,doc,draft,
            sketcher::ExternalReferenceKind::Edge,box,edge.reference.semantic_key,{},body);
        require(!prepared.broken&&prepared.cached_points.size()>=2,
                "New Sketch could not project an earlier original edge");
        draft.add_external_reference(prepared);
        static_cast<void>(draft.add_external_profile_geometry(prepared.id));
        require(draft.segments.size()==1 && state->session.document().history.size()==original.history.size(),
                "Draft outline conversion changed committed history");
        const auto shifted_body=context.body_history.create_body("Translated draft target");
        auto shifted=*context.body_history.find(shifted_body);
        shifted.scope.placement.x=17;shifted.scope.placement.y=-9;
        context.body_history.update_body(shifted);
        state->session.commit(context,calculated);
        const auto local=workspace::prepare_sketch_external_reference(live,doc,draft,
            sketcher::ExternalReferenceKind::Edge,box,edge.reference.semantic_key,{},shifted_body);
        auto expected=prepared.cached_points;
        for(auto& point:expected){point[0]-=17;point[1]+=9;}
        require(local.cached_points==expected,
                "Draft reference projection lost the translated Body coordinate frame");
        state->session.commit(original,calculated);
    }
    // An exact rational source deliberately has only two display sample points.
    auto cached=state->session.calculated_boundaries();kernel::ViewerEdge spline;
    spline.reference={box,"test-original-quarter-circle",{}};spline.points={{1,0,0},{0,1,0}};
    spline.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
    cached.back().mesh.original_references.edges.push_back(spline);state->session.commit(state->session.document(),std::move(cached));
    const auto spline_ref=command("sketch.reference.create",{{"kind","edge"},{"owner",box},{"key",spline.reference.semantic_key}}).at("reference").get<std::string>();
    const auto curve=command("sketch.reference.project",{{"reference",spline_ref}}).at("geometry").get<std::string>();
    require(std::ranges::any_of(current().arcs,[&](const auto& arc){return arc.id==curve;}),"Exact circular source did not become a native arc");
    for(int i=0;i<=256;++i){const auto p=kernel::bspline_value(current().supporting_curve(curve),i/256.);require(std::abs(p.x*p.x+p.y*p.y-1)<1e-12,"Rational source lost exact weights");}
    const auto verify_display=[&] {
        const auto& sketch=current();const auto before=sketch.serialized();const auto mesh=sketch.viewer_mesh();
        const auto edge=std::ranges::find(mesh.edges,"external_edge:"+spline_ref,[](const auto& e){return e.reference.semantic_key;});
        require(edge!=mesh.edges.end()&&edge->exact_spline&&edge->points.size()==129,
            "Exact external curve was displayed as coarse cache chords");
        const auto& reference=*std::ranges::find(sketch.external_references,spline_ref,&sketcher::SketchExternalReference::id);
        for(unsigned i=0;i<edge->points.size();++i) {
            const auto p=kernel::bspline_value(*reference.exact_spline,double(i)/128);
            const auto expected=sketch.world_point(p.x,p.y),actual=edge->points[i];
            require(std::hypot(std::hypot(actual.x-expected.x,actual.y-expected.y),actual.z-expected.z)<1e-10,
                "External display lost the exact curve or Sketch frame");
        }
        require(sketch.serialized()==before,"External display modified the read-only source");
    };
    verify_display();
    const auto offset=command("sketch.offset.create",{{"source",curve},{"distance_mm",.1},{"flipped",true}}).at("geometry").get<std::string>();
    command("sketch.curve.retain",{{"geometry",curve},{"intervals",{{.1,.9}}}});
    cached=state->session.calculated_boundaries();auto& moved=cached.back().mesh.original_references.edges.back();for(auto& p:moved.points)p.x+=.01;for(auto& p:moved.exact_spline->poles)p.x+=.01;
    const auto expected=sketcher::offset_curve_geometry(*moved.exact_spline,-.1);state->session.commit(state->session.document(),std::move(cached));
    command("sketch.reference.refresh");require(!current().external_references.back().broken,"Trimmed source became broken after a small change");
    verify_display();
    for(int i=0;i<=256;++i){const auto a=kernel::bspline_value(current().supporting_curve(offset),i/256.),b=kernel::bspline_value(expected,i/256.);require(std::hypot(a.x-b.x,a.y-b.y)<1e-8,"Offset did not follow trimmed exact projection");}
    const auto retained=current().points;cached=state->session.calculated_boundaries();cached.back().mesh.original_references.edges.pop_back();state->session.commit(state->session.document(),std::move(cached));
    require(command("sketch.reference.refresh").at("broken_references").size()==1 && current().points==retained,"Missing source destroyed its last valid geometry");
    command("sketch.reference.delete",{{"reference",spline_ref}});require(current().points==retained,"Detaching broken spline changed its poles");
    run(host,"save");const auto reopened=document::PartDocument::load(directory/"references.prtz");const auto saved_sketch=std::ranges::find(reopened.sketches,current().id,&sketcher::Sketch::id); require(saved_sketch!=reopened.sketches.end() && saved_sketch->serialized()==current().serialized(),"Native Part lost projected geometry or references");
    // Embedded Helical profiles obey the same preceding-source rule.
    auto embedded=sketcher::Sketch::create_default();auto helix=document::PartDocument::create_helical_sweep_container();embedded.owner_container_id=helix.id;helix.helical.sketches[2]=embedded.serialized();
    auto next=state->session.document();next.insert_history_entry(document::PartHistoryKind::Feature,helix.id);next.history.push_back(helix);state->session.commit(std::move(next),state->session.calculated_boundaries());
    auto embedded_args=edge_args;embedded_args["sketch"]=embedded.id;run(host,"sketch.reference.create",embedded_args);
    require(workspace::document_sketch(live,doc,embedded.id).external_references.front().source_owner_id==box,"Embedded Helical profile did not resolve earlier original source");
    run(host,"undo");run(host,"undo");
    // The same source appears through two nested occurrences. Use persisted
    // snapshots even when the source filenames are deliberately unavailable.
    kernel::BodySnapshot snapshot=state->session.calculated_boundaries().back();
    auto nested=assembly::AssemblyDocument::create_default();auto first=assembly::AssemblyDocument::create_part_occurrence("First",doc,"missing.prtz",snapshot);
    auto second=assembly::AssemblyDocument::create_part_occurrence("Second",doc,"missing.prtz",snapshot);first.placement.x=20;second.placement.y=30;nested.components={first,second};
    auto top=assembly::AssemblyDocument::create_default();auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Nested",nested.document_id,"missing.asmz",nested);parent.placement.x=100;parent.placement.rotation_z=90;top.components={parent};
    live.add_assembly(top,directory/"references.asmz");run(host,"activate",{{"document",top.document_id}});
    const auto top_sketch=run(host,"sketch.create",{{"name","Assembly projection"}}).data.at("sketch").get<std::string>();
    const auto before_snapshot=live.open_assembly(top.document_id)->session.document().components.front().calculated_source;
    for(const auto& occurrence:{first,second}) {
        auto args=edge_args;args["sketch"]=top_sketch;const auto path=assembly::InstancePath{{parent.occurrence_id,occurrence.occurrence_id}};args["instance_path"]=path.encoded();
        run(host,"sketch.reference.create",args);const auto projected=workspace::document_sketch(live,top.document_id,top_sketch);
        const auto world=live.occurrence_point_to_scene(top.document_id,path,edge.points.front());const auto local=projected.local_point(world);
        require(projected.external_references.back().source_instance_path==path.encoded() && std::hypot(projected.external_references.back().cached_points.front()[0]-local[0],projected.external_references.back().cached_points.front()[1]-local[1])<1e-8,"Projection resolved the wrong nested occurrence");
    }
    require(live.size()==2 && live.open_assembly(top.document_id)->session.document().components.front().calculated_source.shares_with(before_snapshot),"Projection loaded dependencies or replaced the Assembly snapshot");
    auto ambiguous=edge_args;ambiguous["sketch"]=top_sketch;require(!host.execute({{"command","sketch.reference.create"},{"arguments",ambiguous}}).ok,"Assembly source without occurrence was accepted");
    run(host,"save");const auto loaded=assembly::AssemblyDocument::load(directory/"references.asmz");require(loaded.sketches.back().external_references.size()==2,"Assembly lost repeated reference occurrences");
}
}
int main(){try{kernel::OcctKernel kernel;
if(const auto* file=std::getenv("ZIMA_FORM_SOURCE_MOTION_DOCUMENT")) {
    std::vector<kernel::BodyResult> bodies;auto part=document::PartDocument::load(file,&bodies);
    workspace::Workspace live;live.add_part(part,bodies,fs::path(file));auto* state=live.open_part(part.document_id);
    const auto dependent=std::ranges::find_if(part.sketches,[](const auto& s){return !s.external_references.empty();})->id;
    const auto verify=[&] {
        const auto& document=state->session.document();const auto& s=*std::ranges::find(document.sketches,dependent,&sketcher::Sketch::id);const auto& ref=s.external_references.front();
        const auto& source=*std::ranges::find(document.sketches,ref.source_owner_id,&sketcher::Sketch::id);
        const auto* point=source.find_point(ref.source_semantic_key.substr(6));const auto expected=s.local_point(source.world_point(point->x,point->y));
        require(!ref.broken&&std::hypot(ref.cached_points.front()[0]-expected[0],ref.cached_points.front()[1]-expected[1])<1e-6,"FORM regenerated cache is stale");
        const auto k=std::ranges::find_if(s.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference&&c.second_point_id==ref.id;});const auto* contact=s.find_point(k->first_point_id);
        require(std::hypot(contact->x-expected[0],contact->y-expected[1])<1e-6,"FORM regenerated native endpoint is stale");
        for(const auto& body:state->session.calculated_boundaries())require(body.calculation_errors.empty(),"FORM source motion left a failed feature");
        auto checked=s;const auto result=checked.solve();require(result.status!=sketcher::SolveStatus::Conflicting&&result.maximum_residual<1e-6,"FORM regenerated equations fail");
        for(const auto& d:s.dimensions)if(d.driving){const auto* p=s.find_point(d.first_point_id);require(std::abs((d.kind==sketcher::DimensionKind::DistanceSymmetric?2*std::abs(p->x):std::abs(p->y))-d.value)<1e-6,"FORM regeneration changed authored dimension");}
    };
    static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));verify();
    state->session.document().save("build/FORM-KOULE-source-motion-repaired.prtz",state->session.calculated_boundaries());
    for(double diameter:{32.,36.,35.}) {
        auto next=state->session.document();require(next.sketches.front().set_dimension_value(next.sketches.front().dimensions.front().id,diameter),"FORM source diameter edit failed");state->session.commit(std::move(next),state->session.calculated_boundaries());
        static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));verify();
        require(state->session.undo(),"FORM source refresh Undo unavailable");require(state->session.redo(),"FORM source refresh Redo unavailable");verify();
    }
    std::cout<<"FORM document source movement, repeated regeneration, dimension preservation and Undo/Redo passed\n";return 0;
}
if(const auto* file=std::getenv("ZIMA_FORM_REFERENCE_DIAGNOSTIC")) {
    std::vector<kernel::BodyResult> bodies;auto part=document::PartDocument::load(file,&bodies);
    workspace::Workspace live;live.add_part(part,bodies,fs::path(file));
    auto sketch=part.sketches.back();
    const auto owners=workspace::sketch_external_reference_source_owners(part,sketch.id);
    for(const auto& ref:sketch.external_references)std::cout<<"REF source="<<ref.source_owner_id<<" allowed="<<owners.contains(ref.source_owner_id)<<" broken="<<ref.broken<<'\n';
    std::cout<<"SNAPSHOT changed="<<workspace::refresh_sketch_reference_snapshot(live,part.document_id,sketch)<<'\n';
    for(const auto& ref:sketch.external_references)std::cout<<"REF after refresh broken="<<ref.broken<<'\n';
    const auto result=workspace::regenerate_part(live,kernel,part.document_id);
    const auto* current=live.open_part(part.document_id);
    for(const auto& ref:current->session.document().sketches.back().external_references)require(!ref.broken,"Regeneration lost existing standalone Sketch source point");
    for(const auto& body:current->session.calculated_boundaries())require(body.calculation_errors.empty(),"FORM Revolution calculation failed");
    require(current->session.document().sketches.back().points==part.sketches.back().points,"Reference repair changed authored Sketch geometry");
    std::cout<<"REGENERATE refs_changed="<<result.references_changed<<'\n';
    for(const auto& ref:current->session.document().sketches.back().external_references)std::cout<<"REF after regenerate broken="<<ref.broken<<'\n';
    for(const auto& body:current->session.calculated_boundaries())for(const auto& [id,error]:body.calculation_errors)std::cout<<"CALC ERROR "<<id<<": "<<error<<'\n';
    auto& session=live.open_part(part.document_id)->session;
    require(session.undo()&&session.document().sketches.back().external_references.front().broken,"Reference repair Undo failed");
    require(session.redo()&&!session.document().sketches.back().external_references.front().broken,"Reference repair Redo failed");
    current->session.document().save("build/FORM-KOULE-reference-regenerated.prtz",current->session.calculated_boundaries());
    const auto reopened=document::PartDocument::load("build/FORM-KOULE-reference-regenerated.prtz");
    require(reopened.sketches.back().serialized()==current->session.document().sketches.back().serialized(),"Repaired FORM references did not survive save/reopen");
    static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
    for(const auto& ref:live.open_part(part.document_id)->session.document().sketches.back().external_references)require(!ref.broken,"Repeated regeneration broke FORM source point");
    return 0;
}
const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-sketch-refs-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Original Sketch references, exact rational projection, trim/offset refresh, detach, atomicity and nested occurrences passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
