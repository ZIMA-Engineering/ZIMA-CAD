#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void run_sloping_line_incidence_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    std::size_t count=0;
    for(bool swap:{false,true})for(double sx:{-1.,1.})for(double sy:{-1.,1.})
    for(bool external:{false,true})for(bool locked:{false,true})for(bool full:{false,true})
    for(bool driving:{false,true})for(int driver=0;driver<4;++driver) {
        try {
            auto s=Sketch::create_default();const auto transform=[&](double x,double y){return swap?std::array{sy*y,sx*x}:std::array{sx*x,sy*y};};
            std::array<std::array<double,2>,4> vertices={transform(5,0),transform(5-.5*57/56,57),transform(10,57),transform(10,0)};
            std::vector<std::string> lines;
            for(int i=0;i<4;++i)lines.push_back(s.add_segment(vertices[i][0],vertices[i][1],vertices[(i+1)%4][0],vertices[(i+1)%4][1],1e-8));
            std::vector<std::string> points;for(const auto& l:s.segments)points.push_back(l.first_point_id);
            for(int i:{1,3})static_cast<void>(s.add_point_pair_constraint(s.segments[i].first_point_id,s.segments[i].second_point_id,swap?ConstraintKind::Vertical:ConstraintKind::Horizontal));
            static_cast<void>(s.add_point_pair_constraint(points[2],points[3],swap?ConstraintKind::Horizontal:ConstraintKind::Vertical));
            std::string support=swap?"sketch_axis:y":"sketch_axis:x";
            if(external) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="read-only-source";ref.source_owner_id="support";ref.source_semantic_key="seating";
                ref.cached_points={transform(-20,0),transform(20,0)};s.add_external_reference(ref);support=ref.id;
            }
            static_cast<void>(s.add_point_on_line_constraint(points[0],support));
            const auto pos=transform(4.5,56);const auto contact=s.add_point(pos[0],pos[1],1e-8,true);
            static_cast<void>(s.add_point_on_line_constraint(contact,lines[0]));
            std::vector<std::string> dimensions;
            for(const auto& [point,x]:{std::pair{points[0],true},std::pair{contact,true},std::pair{contact,false},std::pair{points[1],false},std::pair{points[2],true}}) {
                auto d=s.create_axis_dimension(point,(x!=swap)?"sketch_axis:y":"sketch_axis:x");d.locked=locked;s.apply_dimension(d);dimensions.push_back(d.id);
            }
            if(!full)std::erase_if(s.dimensions,[&](const auto& d){return d.id==dimensions.back();});
            auto d=*std::ranges::find(s.dimensions,dimensions[driver],&SketchDimension::id);d.driving=driving;d.locked=locked&&driving;s.apply_dimension(d);
            const auto initial=d.value;const auto sources=s.external_references;
            const auto verify=[&] {
                for(const auto& c:s.constraints) {
                    const auto* p=s.find_point(c.first_point_id);
                    if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical) {
                        const auto* q=s.find_point(c.second_point_id);check(std::abs(c.kind==ConstraintKind::Horizontal?p->y-q->y:p->x-q->x)<1e-8,"Sloping line H/V changed");
                    } else if(c.kind==ConstraintKind::PointOnLine) {
                        std::array<double,2> a{},v{};
                        if(c.geometry_id==support){a=transform(0,0);v=transform(1,0);}
                        else {const auto& l=s.segments.front();const auto* q=s.find_point(l.first_point_id);const auto* r=s.find_point(l.second_point_id);a={q->x,q->y};v={r->x-q->x,r->y-q->y};}
                        check(std::abs((p->x-a[0])*v[1]-(p->y-a[1])*v[0])/std::hypot(v[0],v[1])<1e-8,"Sloping native line contact changed");
                    } else throw std::runtime_error("Unverified sloping line constraint");
                }
                for(const auto& dim:s.dimensions){const auto* p=s.find_point(dim.first_point_id);check(std::abs((dim.kind==DimensionKind::DistanceX?p->x:p->y)-dim.value)<1e-8,"Sloping line ordinate changed");}
                check(s.external_references==sources,"Sloping line changed read-only source");
                auto solved=s;const auto status=solved.solve();check(status.maximum_residual<1e-8&&status.remaining_degrees_of_freedom==(!full)+(!driving),"Sloping line solve/DOF failed");
            };
            verify();
            for(double scale:driver==2?std::array{1.01,.99,1.}:std::array{1.02,1.04,1.}) {
                const auto before=s.serialized();check(s.set_dimension_value(d.id,initial*scale),"Sloping line coordinate edit was rejected");verify();
                if(!driving)check(before==s.serialized(),"Sloping line reference changed geometry");
                s=Sketch::from_serialized(s.serialized());verify();
            }
            if(driver==2&&driving) {
                const auto before=s.serialized();check(!s.set_dimension_value(d.id,initial*1.04)&&s.serialized()==before,"Contact outside the finite native line was accepted");
            }
            for(double delta:{.05,-.05,.1}) {
                const auto previous=*s.find_point(points[2]);const auto before=s.serialized();const auto move=transform(delta,0);
                const bool moved=s.move_point(points[2],previous.x+move[0],previous.y+move[1]);
                if(!locked||!full)check(moved&&std::hypot(s.find_point(points[2])->x-previous.x,s.find_point(points[2])->y-previous.y)>1e-6,"Free sloping line handle did not move");
                else check(!moved&&s.serialized()==before,"Locked sloping line handle escaped");verify();
            }
            ++count;
        }catch(const std::exception& error){throw std::runtime_error("Sloping line driver="+std::to_string(driver)+" swap="+std::to_string(swap)+" sx="+std::to_string(sx)+" sy="+std::to_string(sy)+" external="+std::to_string(external)+" locked="+std::to_string(locked)+" full="+std::to_string(full)+" driving="+std::to_string(driving)+": "+error.what());}
    }
    std::cout<<count<<" sloping native line variants: successive ordinates, independent equations, transformed native/external supports, lock/reference/DOF states, drags and serialized reopen passed\n";
}
