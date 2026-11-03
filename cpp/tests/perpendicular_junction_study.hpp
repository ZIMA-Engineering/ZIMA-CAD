#pragma once

#include <zima/document/part_document.hpp>
#include <zima/sketcher/sketch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

inline int run_perpendicular_junction_study() try {
    using namespace zima;
    using namespace zima::sketcher;
    const auto check=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    std::size_t cases{};
    for(const bool swap:{false,true})for(const double sx:{-1.,1.})
    for(const double sy:{-1.,1.})for(const bool reverse:{false,true})
    for(const bool external:{false,true})for(const int dimension_kind:{0,1,2})
    for(const bool driving:{false,true})for(const bool locked:{false,true}) {
        if(dimension_kind==0&&(driving||locked))continue;
        if(!driving&&locked)continue;
        const auto transform=[&](double x,double y) {
            return swap?std::array{sy*y,sx*x}:std::array{sx*x,sy*y};
        };
        const auto a=transform(0.,0.),b=transform(10.,0.),c=transform(5.,5.);
        auto sketch=Sketch::create_default();
        const auto base=sketch.add_segment(a[0],a[1],b[0],b[1],1.0e-8);
        const auto first=sketch.add_segment(a[0],a[1],c[0],c[1],1.0e-8);
        const auto second=sketch.add_segment(b[0],b[1],c[0],c[1],1.0e-8);
        const auto first_segment=*std::ranges::find(sketch.segments,first,&SketchSegment::id);
        const auto second_segment=*std::ranges::find(sketch.segments,second,&SketchSegment::id);
        const auto a_id=first_segment.first_point_id;
        const auto junction=first_segment.second_point_id;
        const auto b_id=second_segment.first_point_id;
        check(second_segment.second_point_id==junction,
            "Perpendicular fixture did not share one native junction point");
        sketch.set_point_fixed(a_id,true);
        if(external) {
            auto reference=Sketch::create_external_reference(
                ExternalReferenceKind::Point);
            reference.source_document_id="read-only-source";
            reference.source_owner_id="source-feature";
            reference.source_semantic_key="source-point";
            reference.cached_points={b};
            sketch.add_external_reference(reference);
            static_cast<void>(sketch.add_point_reference_constraint(
                b_id,reference.id));
        } else sketch.set_point_fixed(b_id,true);
        static_cast<void>(sketch.add_segment_pair_constraint(
            reverse?second:first,reverse?first:second,
            ConstraintKind::Perpendicular));

        std::string dimension_id;
        if(dimension_kind==1) {
            auto dimension=sketch.create_line_pair_dimension(
                reverse?first:base,reverse?base:first,
                DimensionKind::AngleBetween);
            dimension.driving=driving;dimension.locked=locked;
            dimension_id=dimension.id;sketch.apply_dimension(dimension);
        } else if(dimension_kind==2) {
            auto dimension=sketch.create_segment_dimension(
                reverse?second:first);
            dimension.driving=driving;dimension.locked=locked;
            dimension_id=dimension.id;sketch.apply_dimension(dimension);
        }
        const auto references=sketch.external_references;
        const auto verify=[&](const Sketch& value) {
            const auto* pa=value.find_point(a_id);
            const auto* pb=value.find_point(b_id);
            const auto* pc=value.find_point(junction);
            check(pa&&pb&&pc,"Perpendicular junction lost a native point");
            check(std::hypot(pa->x-a[0],pa->y-a[1])<1.0e-8&&
                  std::hypot(pb->x-b[0],pb->y-b[1])<1.0e-8,
                "Perpendicular junction moved an immutable endpoint");
            const double ux=pa->x-pc->x,uy=pa->y-pc->y;
            const double vx=pb->x-pc->x,vy=pb->y-pc->y;
            check(std::abs(ux*vx+uy*vy)<1.0e-7,
                "Perpendicular junction lost its right-angle equation");
            const double cx=(pa->x+pb->x)*.5,cy=(pa->y+pb->y)*.5;
            const double radius=std::hypot(pb->x-pa->x,pb->y-pa->y)*.5;
            check(std::abs(std::hypot(pc->x-cx,pc->y-cy)-radius)<1.0e-7,
                "Perpendicular junction left its exact Thales-circle locus");
            auto solved=value;const auto status=solved.solve();
            check(status.maximum_residual<1.0e-8&&
                  status.remaining_degrees_of_freedom==
                    (dimension_kind==0||!driving?1:0),
                "Perpendicular junction has a wrong residual or DOF count");
            check(value.external_references==references,
                "Perpendicular junction modified a read-only external support");
            if(dimension_id.empty())return;
            const auto dimension=std::ranges::find(
                value.dimensions,dimension_id,&SketchDimension::id);
            check(dimension!=value.dimensions.end(),
                "Perpendicular junction lost its dimension");
            if(dimension_kind==2) {
                const auto* p=value.find_point(dimension->first_point_id);
                const auto* q=value.find_point(dimension->second_point_id);
                check(p&&q,
                    "Perpendicular arm-length dimension lost an endpoint");
                check(std::abs(std::hypot(q->x-p->x,q->y-p->y)-
                    dimension->value)<1.0e-7,
                    "Perpendicular arm-length dimension equation changed");
            } else {
                const auto angle_segment=[&](const std::string& id) {
                    const auto segment=std::ranges::find(
                        value.segments,id,&SketchSegment::id);
                    const auto* p=value.find_point(segment->first_point_id);
                    const auto* q=value.find_point(segment->second_point_id);
                    return std::array{q->x-p->x,q->y-p->y};
                };
                const auto u=angle_segment(dimension->geometry_id);
                const auto v=angle_segment(dimension->second_geometry_id);
                const double measured=std::acos(std::clamp(
                    (u[0]*v[0]+u[1]*v[1])/
                    (std::hypot(u[0],u[1])*std::hypot(v[0],v[1])),
                    -1.,1.))*180./std::acos(-1.);
                check(std::abs(measured-dimension->value)<1.0e-7,
                    "Perpendicular angle dimension equation changed");
            }
        };
        verify(sketch);
        if(!dimension_id.empty()) {
            if(driving) {
                const auto initial=std::ranges::find(sketch.dimensions,
                    dimension_id,&SketchDimension::id)->value;
                const std::array values=dimension_kind==1
                    ?std::array{40.,50.,initial}
                    :std::array{initial*.9,initial*1.1,initial};
                for(const double value:values) {
                    check(sketch.set_dimension_value(dimension_id,value),
                        "Perpendicular dimension edit was rejected");
                    verify(sketch);
                    sketch=Sketch::from_serialized(sketch.serialized());
                    verify(sketch);
                }
            } else {
                const auto before=sketch.serialized();
                check(sketch.set_dimension_value(dimension_id,123.)&&
                      sketch.serialized()==before,
                    "Reference dimension drove a perpendicular junction");
            }
        }
        for(const auto cursor:std::array{
                std::array{7.,8.},std::array{3.,8.},std::array{5.,6.}}) {
            const auto target=transform(cursor[0],cursor[1]);
            const auto before=sketch.serialized();
            const auto old=*sketch.find_point(junction);
            const bool moved=sketch.move_point(
                junction,target[0],target[1]);
            if(driving&&locked)check(!moved&&sketch.serialized()==before,
                "Locked perpendicular junction escaped during dragging");
            else {
                check(moved,"Free perpendicular junction drag was rejected");
                const auto* current=sketch.find_point(junction);
                check(std::hypot(current->x-old.x,current->y-old.y)>1.0e-5,
                    "Projected perpendicular junction drag did not move");
                verify(sketch);
                sketch=Sketch::from_serialized(sketch.serialized());
                verify(sketch);
            }
        }
        ++cases;
    }

    // One native Part transaction proves the same junction survives actual
    // Undo/Redo and the native archive, rather than only Sketch JSON roundtrip.
    auto persisted=Sketch::create_default();
    const auto base=persisted.add_segment(0,0,10,0,1.0e-8);
    const auto first=persisted.add_segment(0,0,5,5,1.0e-8);
    const auto second=persisted.add_segment(10,0,5,5,1.0e-8);
    const auto junction=persisted.segments[1].second_point_id;
    persisted.set_point_fixed(persisted.segments[0].first_point_id,true);
    persisted.set_point_fixed(persisted.segments[0].second_point_id,true);
    static_cast<void>(persisted.add_segment_pair_constraint(
        first,second,ConstraintKind::Perpendicular));
    auto dimension=persisted.create_line_pair_dimension(
        base,first,DimensionKind::AngleBetween);
    persisted.apply_dimension(dimension);
    auto owner=document::PartDocument::create_sketch_container();
    owner.feature.sketch_id=persisted.id;
    persisted.owner_container_id=owner.id;
    auto part=document::PartDocument::create_default();
    part.sketches={persisted};part.history={owner};
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Perpendicular junction"));
    graph.insert({document::PartHistoryKind::Feature,owner.id});
    part.set_body_history(graph);part.resolve_constructions();
    document::DocumentSession session(part);
    auto edited=part;
    check(edited.sketches.front().move_point(junction,7,8),
        "Document perpendicular junction drag failed");
    const auto changed=edited.sketches.front().serialized();
    session.commit(edited);
    check(session.undo()&&
          session.document().sketches.front().serialized()==
            persisted.serialized(),
        "Perpendicular junction Undo lost its native state");
    check(session.redo()&&
          session.document().sketches.front().serialized()==changed,
        "Perpendicular junction Redo lost its edited state");
    const auto path=std::filesystem::current_path()/
        ("perpendicular-junction-"+owner.id+".prtz");
    session.document().save(path);
    const auto reopened=document::PartDocument::load(path);
    std::filesystem::remove(path);
    check(reopened.sketches.front().serialized()==changed,
        "Perpendicular junction changed after native save/reopen");
    std::cout<<cases<<" perpendicular junction variants: arbitrary projected drags, both orders, orientations, native/external anchors, angle/arm dimensions, driving/reference/lock states, DOF, serialization, Undo/Redo and native reopen passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"Perpendicular junction: "<<error.what()<<'\n';
    return 1;
}
