#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Finite slot matrix: direction, selection order, R/D, contact direction and
// native/free versus read-only external centre support. Equations are checked
// after each successive numeric edit and drag, not just after initial solve.
inline void run_slot_direction_studies() {
    using namespace zima::sketcher;
    const auto check=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };
    unsigned variants{}, drags{}, edits{}, rejected_drags{};
    for(bool vertical:{false,true})for(bool reverse:{false,true})
    for(bool diameter:{false,true})for(double sign:{-1.,1.})for(bool external:{false,true})for(bool euclidean:{false,true}) {
        auto s=Sketch::create_default();
        const auto ids=s.add_slot(10,20,10+sign*32,44,5);
        const auto first=s.arcs[0].center_point_id,second=s.arcs[1].center_point_id;
        static_cast<void>(s.add_point_pair_constraint(reverse?second:first,reverse?first:second,
            vertical?ConstraintKind::Vertical:ConstraintKind::Horizontal));
        if(external) {
            const auto p=*s.find_point(first);
            SketchExternalReference support;support.id="slot-centre-support";
            support.source_document_id="source";support.source_owner_id="source-feature";
            support.source_semantic_key="point:source-centre";
            support.kind=ExternalReferenceKind::Point;support.cached_points={{p.x,p.y}};
            s.add_external_reference(support);
            static_cast<void>(s.add_point_reference_constraint(first,support.id));
        }
        const auto references=s.external_references;
        const auto verify=[&](Sketch& value) {
            const auto solved=value.solve();
            check(solved.status!=SolveStatus::Invalid&&solved.status!=SolveStatus::Conflicting&&solved.maximum_residual<1e-8,
                "Slot H/V matrix did not solve");
            const auto* a=value.find_point(first);const auto* b=value.find_point(second);
            check(a&&b&&std::abs(vertical?a->x-b->x:a->y-b->y)<1e-7,"Slot centre H/V equation failed");
            check(value.points.size()==6&&value.arcs.size()==2&&value.segments.size()==2,
                "Slot H/V edit changed shared native topology");
            check(value.external_references==references,"Slot edit changed read-only external geometry");
            if(external)check(std::hypot(a->x-references.front().cached_points.front()[0],
                a->y-references.front().cached_points.front()[1])<1e-7,"Slot centre escaped its external anchor");
            check(std::abs(value.arcs[0].radius-value.arcs[1].radius)<1e-7,"Slot equal radii failed");
            for(const auto& arc:value.arcs) {
                const auto* c=value.find_point(arc.center_point_id);
                for(const auto& endpoint:{arc.start_point_id,arc.end_point_id}) {
                    const auto* p=value.find_point(endpoint);
                    check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-7,"Slot arc endpoint radius failed");
                    for(const auto& line:value.segments)if(line.first_point_id==endpoint||line.second_point_id==endpoint) {
                        const auto* q=value.find_point(line.first_point_id);const auto* r=value.find_point(line.second_point_id);
                        check(std::abs((p->x-c->x)*(r->x-q->x)+(p->y-c->y)*(r->y-q->y))<1e-6,
                            "Slot H/V edit broke shared-point tangency");
                    }
                }
            }
            for(const auto& d:value.dimensions) {
                if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter) {
                    check(std::abs(d.value-value.arcs[0].radius*(d.kind==DimensionKind::Diameter?2:1))<1e-7,
                        "Slot R/D annotation disagrees with its arc");
                } else if(d.kind==DimensionKind::Distance||d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY) {
                    const auto* p=value.find_point(d.first_point_id);const auto* q=value.find_point(d.second_point_id);
                    const double actual=d.kind==DimensionKind::Distance?std::hypot(p->x-q->x,p->y-q->y):
                        (d.kind==DimensionKind::DistanceX?q->x-p->x:q->y-p->y);
                    check(std::abs(d.value-actual)<1e-7,"Slot centre distance annotation disagrees with geometry");
                }
            }
        };
        verify(s);
        auto radial=diameter?s.create_arc_diameter_dimension(ids[0]):s.create_arc_radius_dimension(ids[0]);
        s.apply_dimension(radial);
        auto length=s.create_point_dimension(reverse?second:first,reverse?first:second,
            euclidean?DimensionKind::Distance:vertical?DimensionKind::DistanceY:DimensionKind::DistanceX);
        s.apply_dimension(length);
        for(double radius:{6.,8.,5.}) {
            check(s.set_dimension_value(radial.id,radius*(diameter?2:1)),"Slot R/D repeated edit rejected");++edits;verify(s);
            check(s.set_dimension_value(length.id,std::copysign(30.+radius,length.value)),"Slot centre distance repeated edit rejected");++edits;verify(s);
        }
        auto annotation=s.create_point_dimension(first,second,euclidean?(vertical?DimensionKind::DistanceY:DimensionKind::DistanceX):DimensionKind::Distance);annotation.driving=false;
        s.apply_dimension(annotation);verify(s);
        const auto editable=s;
        const double locked_length=std::abs(std::ranges::find(s.dimensions,length.id,&SketchDimension::id)->value);
        for(bool locked:{false,true}) {
            auto dragged=editable;
            for(auto& d:dragged.dimensions)if(d.driving)d.locked=locked;
            for(const auto& point:editable.points)for(const auto delta:{std::array{2.,3.},std::array{-1.,2.},std::array{1.,-2.}}) {
                if(external&&point.id==first)continue;
                const auto before=dragged.serialized();const auto p=*dragged.find_point(point.id);
                const bool moved=dragged.move_point(point.id,p.x+delta[0],p.y+delta[1]);
                if(moved) {
                    const auto* q=dragged.find_point(point.id);
                    check(std::hypot(q->x-p.x,q->y-p.y)>1e-6,"Slot accepted a drag without moving");++drags;
                } else {++rejected_drags;check(dragged.serialized()==before,"Rejected slot drag changed document state");}
                verify(dragged);
                if(locked) {
                    check(std::abs(dragged.arcs[0].radius-editable.arcs[0].radius)<1e-7,"Locked slot radius escaped");
                    const auto* a=dragged.find_point(first);const auto* b=dragged.find_point(second);
                    check(std::abs(std::hypot(a->x-b->x,a->y-b->y)-locked_length)<1e-7,
                        "Locked slot length escaped");
                }
            }
            auto reopened=Sketch::from_serialized(dragged.serialized());verify(reopened);
        }
        auto fixed=editable;for(auto& p:fixed.points)p.fixed=true;
        const auto unchanged=fixed.serialized();const auto p=*fixed.find_point(second);
        check(!fixed.move_point(second,p.x+3,p.y+4)&&fixed.serialized()==unchanged,"Fixed slot escaped on drag");
        check(!fixed.set_dimension_value(radial.id,19)&&fixed.serialized()==unchanged,"Impossible slot edit changed document");
        auto duplicate=editable;const auto original=duplicate.serialized();bool rejected=false;
        try {static_cast<void>(duplicate.add_point_pair_constraint(first,second,
            vertical?ConstraintKind::Vertical:ConstraintKind::Horizontal));}catch(const std::exception&){rejected=true;}
        check(rejected&&duplicate.serialized()==original,"Redundant slot H/V was not rejected atomically");
        ++variants;
    }
    std::cout<<"Slot H/V: "<<variants<<" variants, "<<edits<<" numeric edits, "<<drags
        <<" actual drags, "<<rejected_drags<<" atomic drag rejections; independent centre/tangent/R/D/distance equations, locks, references, rejection and reopen passed\n";
}
