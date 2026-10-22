#pragma once
#include "../app/sketch_inference_policy.hpp"

inline void run_sketch_corner_annotation_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    unsigned cases=0,rejected_reference_locks=0;
    for(bool vertical:{false,true})for(double sign:{-1.,1.})for(bool external:{false,true})
    for(bool diameter:{false,true})for(bool locked:{false,true})for(bool driving:{false,true}) {
        auto s=Sketch::create_default();
        const auto first=s.add_segment(0,0,vertical?0:20,vertical?20:0);
        const auto second=s.add_segment(0,0,vertical?20*sign:0,vertical?0:20*sign);
        const auto corner=s.segments.front().first_point_id;
        static_cast<void>(s.add_segment_constraint(first,vertical?ConstraintKind::Vertical:ConstraintKind::Horizontal));
        static_cast<void>(s.add_segment_constraint(second,vertical?ConstraintKind::Horizontal:ConstraintKind::Vertical));
        static_cast<void>(s.add_segment_pair_constraint(first,second,ConstraintKind::EqualLength));
        std::string contact;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
            ref.source_document_id="readonly-source";ref.source_owner_id="origin-point";ref.source_semantic_key="point";ref.cached_points={{0,0}};
            const auto id=ref.id;s.add_external_reference(ref);contact=s.add_point_reference_constraint(corner,id);
        }else contact=s.add_point_reference_constraint(corner,"sketch_origin");
        const auto constraints=s.constraints;const auto refs=s.external_references;
        const auto radius=s.add_corner_fillet(first,second,2).arc_id;
        auto dimension=diameter?s.create_arc_diameter_dimension(radius):s.create_arc_radius_dimension(radius);
        dimension.locked=locked;dimension.driving=driving;
        if(locked&&!driving) {
            const auto before=s.serialized();bool rejected=false;
            try{s.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&s.serialized()==before,"Locked reference corner dimension was not rejected atomically");
            ++rejected_reference_locks;continue;
        }
        s.apply_dimension(dimension);
        const auto verify=[&] {
            const auto before=s.serialized();const auto mesh=s.viewer_mesh();
            check(s.serialized()==before,"Corner marker publication changed the document");
            check(s.constraints==constraints&&s.external_references==refs,"Corner evaluation changed authored contacts or read-only source");
            for(const auto& c:constraints)check(std::ranges::any_of(mesh.constraint_markers,[&](const auto& marker){return marker.reference.semantic_key=="constraint:"+c.id;}),"Corner evaluation hid an authored constraint marker");
            const auto* p=s.find_point(corner);check(p&&std::hypot(p->x,p->y)<1e-7,"Corner origin contact escaped");
            const auto length=[&](const SketchSegment& line){const auto* a=s.find_point(line.first_point_id);const auto* b=s.find_point(line.second_point_id);return std::hypot(a->x-b->x,a->y-b->y);};
            check(std::abs(length(s.segments[0])-length(s.segments[1]))<1e-7,"Equal design lengths escaped after corner edit");
            const auto profile=s.evaluated_profile_sketch();check(profile.arcs.size()==1,"Rounded profile lost its arc");
            const auto& arc=profile.arcs.front();const auto* center=profile.find_point(arc.center_point_id);
            for(const auto& id:{arc.start_point_id,arc.end_point_id}){const auto* q=profile.find_point(id);check(q&&std::abs(std::hypot(q->x-center->x,q->y-center->y)-arc.radius)<1e-7,"Rounded endpoint violates circle equation");}
        };
        verify();
        for(double r:{3.,1.5,2.5}) {
            if(driving)check(s.set_dimension_value(dimension.id,diameter?2*r:r),"Corner dimension could not be edited repeatedly");
            else static_cast<void>(s.add_corner_fillet(second,first,r));
            verify();
            const auto endpoint=s.segments.front().second_point_id;
            const double target=22+r;
            check(s.move_point(endpoint,vertical?0:target,vertical?target:0),"Underconstrained equal corner leg did not move");verify();
            const auto* moved=s.find_point(endpoint);check(std::abs((vertical?moved->y:moved->x)-target)<1e-6,"Equal corner drag did not reach its permitted position");
            s=Sketch::from_serialized(s.serialized());check(s.solve().status!=SolveStatus::Conflicting,"Rounded contacts failed reopening");verify();
        }
        for(auto& point:s.points)point.fixed=true;
        const auto frozen=s.serialized();const auto end=s.segments.front().second_point_id;
        check(!s.move_point(end,50,50)&&s.serialized()==frozen,"Fixed rounded design escaped or changed on rejected drag");verify();
        ++cases;
    }
    std::cout<<cases<<" corner annotation edit/drag cases and "<<rejected_reference_locks<<" atomic reference-lock rejections passed\n";
}
