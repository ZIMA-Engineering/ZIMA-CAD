#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/document/document_session.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

inline int run_sketch_external_midpoint_study() {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    unsigned cases{};
    for(double angle:{0.,.7,1.5707963267948966})for(bool reverse:{false,true})
    for(bool body_edge:{false,true})for(int state=0;state<3;++state) {
        auto sketch=sketcher::Sketch::create_default();
        const double ux=std::cos(angle),uy=std::sin(angle);
        auto edge=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
        edge.source_document_id="source";edge.source_owner_id="read-only-feature";edge.source_semantic_key="line";edge.body_edge=body_edge;
        edge.cached_points={{2-10*ux,3-10*uy},{2+10*ux,3+10*uy}};
        if(reverse)std::ranges::reverse(edge.cached_points);
        const auto source_cache=edge.cached_points;
        sketch.add_external_reference(edge);
        const auto ends=sketch.add_external_edge_endpoints(edge.id);
        const auto middle=std::ranges::find(sketch.external_references,sketcher::ExternalReferenceKind::EdgeMidpoint,&sketcher::SketchExternalReference::kind);
        check(ends.size()==2&&middle!=sketch.external_references.end()&&sketch.external_references.size()==4,"External Segment did not publish exactly two ends and one midpoint");
        const auto middle_id=middle->id;
        check(std::hypot(middle->cached_points.front()[0]-2,middle->cached_points.front()[1]-3)<1e-8,"External midpoint is not the geometric midpoint");
        const auto stable=sketch.serialized();
        check(sketch.add_external_edge_endpoints(edge.id)==ends&&sketch.serialized()==stable,"Repeated reference preparation duplicated keypoints");
        static_cast<void>(sketch.add_segment(2,3,2+8*ux,3+8*uy));
        const auto line=sketch.segments.front();
        check(sketch.constraints.size()==1&&sketch.constraints.front().kind==sketcher::ConstraintKind::PointReference&&
            sketch.constraints.front().first_point_id==line.first_point_id&&sketch.constraints.front().second_point_id==middle_id,
            "Exact midpoint endpoint did not reuse automatic external C");
        auto length=sketch.create_segment_dimension(line.id);length.driving=state!=0;length.locked=state==2;sketch.apply_dimension(length);
        for(double value:{10.,7.,9.}) {
            if(length.driving)check(sketch.set_dimension_value(length.id,value),"External-midpoint supported length edit failed");
            else check(sketch.move_point(line.second_point_id,2+value*ux,3+value*uy),"Reference dimension blocked a free endpoint");
            const auto* anchor=sketch.find_point(line.first_point_id);
            check(std::hypot(anchor->x-2,anchor->y-3)<1e-7,"Length edit moved the read-only midpoint contact");
            const auto unchanged=sketch.serialized();
            check(!sketch.move_point(line.first_point_id,3,4)&&sketch.serialized()==unchanged,"C-bound midpoint escaped during dragging");
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());
        }
        for(double shift:{4.,-3.,6.}) {
            kernel::ViewerReferenceGeometry geometry;
            kernel::ViewerEdge changed;changed.reference={edge.source_owner_id,edge.source_semantic_key,{}};
            changed.points={sketch.world_point(2+shift-12*ux,3-12*uy),sketch.world_point(2+shift+12*ux,3+12*uy)};
            geometry.edges.push_back(changed);
            const auto source_before=geometry.edges.front().points;
            static_cast<void>(sketch.refresh_external_references("source",geometry,false,&geometry));
            const auto solved=sketch.solve();check(solved.status!=sketcher::SolveStatus::Conflicting&&solved.status!=sketcher::SolveStatus::Invalid,"Changed source midpoint did not solve");
            const auto* p=sketch.find_point(line.first_point_id);
            check(std::abs(p->x-(2+shift))<1e-7&&std::abs(p->y-3)<1e-7,"C did not follow the current source midpoint");
            check(geometry.edges.front().points==source_before,"Dependent midpoint edit changed its external source");
        }
        auto removed=sketch;removed.remove_geometry(middle_id);
        check(removed.external_references.size()==3&&removed.segments.size()==1&&removed.points==sketch.points&&removed.constraints.empty(),"Deleting midpoint changed its edge/endpoints/native points or retained C");
        check(std::ranges::find(removed.external_references,edge.id,&sketcher::SketchExternalReference::id)!=removed.external_references.end(),"Deleting midpoint removed its parent reference");
        auto part=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;
        part.history={owner};part.sketches={sketch};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Midpoint"));graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);
        document::DocumentSession session(part);auto next=part;next.sketches.front().remove_geometry(middle_id);session.commit_metadata(next);
        check(session.undo()&&session.document().sketches.front().serialized()==sketch.serialized()&&session.redo()&&session.document().sketches.front().external_references.size()==3,"Midpoint deletion Undo/Redo lost identity or contacts");
        auto all=sketch;all.remove_geometry(edge.id);check(all.external_references.empty(),"Deleting parent left derived keypoints behind");
        check(edge.cached_points==source_cache,"Keypoint operations modified the source definition");++cases;
    }
    auto curved=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
    curved.cached_points={{0,0},{5,2},{10,0}};
    check(!sketcher::external_edge_point_position(curved,sketcher::ExternalReferenceKind::EdgeMidpoint),"Curved reference was replaced by a chord midpoint");
    std::cout<<"External midpoint: "<<cases<<" orientations, source directions, original/body supports and dimension states; C, successive source edits, independent removal, native serialization and Undo/Redo passed\n";
    return 0;
}
