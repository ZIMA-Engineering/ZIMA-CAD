#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void run_angled_triangle_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    std::size_t cases=0;
    for(bool swap:{false,true})for(double sx:{-1.,1.})for(double sy:{-1.,1.})
    for(bool reverse:{false,true})for(bool external:{false,true})for(bool locked:{false,true})
    for(bool full:{false,true})for(bool driving:{false,true})for(int driver:{0,1}) {
        try {
            auto s=Sketch::create_default();const auto t=[&](double x,double y){return swap?std::array{sy*y,sx*x}:std::array{sx*x,sy*y};};
            const auto a=t(3,-13.5),b=t(5.773502691896258,-13.5),c=t(5.773502691896258,-10.726497308103742);
            const auto slope=s.add_segment(a[0],a[1],c[0],c[1],1e-8);
            const auto side=s.add_segment(c[0],c[1],b[0],b[1],1e-8);
            const auto top=s.add_segment(b[0],b[1],a[0],a[1],1e-8);
            const auto A=s.segments[0].first_point_id,C=s.segments[0].second_point_id,B=s.segments[1].second_point_id;
            if(external) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);ref.source_document_id="read-only-prism";ref.source_owner_id="prism";ref.source_semantic_key="vertex";ref.cached_points={b};
                s.add_external_reference(ref);static_cast<void>(s.add_point_reference_constraint(B,ref.id));
            } else s.set_point_fixed(B,true);
            static_cast<void>(s.add_point_pair_constraint(B,A,swap?ConstraintKind::Vertical:ConstraintKind::Horizontal));
            if(full)static_cast<void>(s.add_point_pair_constraint(C,B,swap?ConstraintKind::Horizontal:ConstraintKind::Vertical));
            auto ordinate=s.create_axis_dimension(A,swap?"sketch_axis:x":"sketch_axis:y");ordinate.locked=locked;s.apply_dimension(ordinate);
            // A native reference arm gives both legitimate participant orders.
            const auto origin=t(0,0),unit=t(1,0);const auto reference=s.add_segment(origin[0],origin[1],unit[0],unit[1],1e-8);
            const auto reference_segment=s.segments.back();s.set_point_fixed(reference_segment.first_point_id,true);s.set_point_fixed(reference_segment.second_point_id,true);
            auto angular=s.create_line_pair_dimension(reverse?slope:reference,reverse?reference:slope,DimensionKind::AngleBetween);angular.locked=locked;s.apply_dimension(angular);
            const auto target=driver?angular.id:ordinate.id;
            auto dimension=*std::ranges::find(s.dimensions,target,&SketchDimension::id);dimension.driving=driving;dimension.locked=locked&&driving;s.apply_dimension(dimension);
            const auto initial=dimension.value;const auto sources=s.external_references;
            const auto verify=[&] {
                const auto* p=s.find_point(A);const auto* q=s.find_point(B);const auto* r=s.find_point(C);
                check(std::hypot(q->x-b[0],q->y-b[1])<1e-8,"Triangle moved its anchor");
                check(std::abs(swap?p->x-q->x:p->y-q->y)<1e-8,"Triangle lost top H/V");
                if(full)check(std::abs(swap?r->y-q->y:r->x-q->x)<1e-8,"Triangle lost side H/V");
                for(const auto& d:s.dimensions)if(d.driving) {
                    if(d.id==ordinate.id)check(std::abs((swap?p->y:p->x)-d.value)<1e-8,"Triangle ordinate equation");
                    else {
                        const auto* first=s.find_point(reverse?A:reference_segment.first_point_id);const auto* second=s.find_point(reverse?C:reference_segment.second_point_id);
                        const auto* third=s.find_point(reverse?reference_segment.first_point_id:A);const auto* fourth=s.find_point(reverse?reference_segment.second_point_id:C);
                        const double dot=(second->x-first->x)*(fourth->x-third->x)+(second->y-first->y)*(fourth->y-third->y);
                        const double lengths=std::hypot(second->x-first->x,second->y-first->y)*std::hypot(fourth->x-third->x,fourth->y-third->y);
                        check(std::abs(std::acos(std::clamp(dot/lengths,-1.,1.))*180/std::acos(-1.)-d.value)<1e-7,"Triangle angle equation");
                    }
                }
                check(s.external_references==sources,"Triangle changed external vertex");
                auto solved=s;const auto status=solved.solve();check(status.maximum_residual<1e-8&&status.remaining_degrees_of_freedom==(!full)+(!driving),"Triangle solve or DOF");
            };
            verify();
            for(double factor:{1.02,1.04,1.}) {
                const auto before=s.serialized();check(s.set_dimension_value(target,initial*factor),"Triangle feasible dimension edit rejected");verify();
                if(!driving)check(before==s.serialized(),"Triangle reference dimension drove geometry");
                s=Sketch::from_serialized(s.serialized());verify();
            }
            for(double delta:{.02,-.02,.03}) {
                const auto before=s.serialized();const auto old=*s.find_point(C);const auto shift=t(full?0:delta,delta);
                const bool moved=s.move_point(C,old.x+shift[0],old.y+shift[1]);
                if(locked&&full&&driving)check(!moved&&s.serialized()==before,"Locked triangle escaped");
                else check(moved&&std::hypot(s.find_point(C)->x-old.x,s.find_point(C)->y-old.y)>1e-7,"Free triangle point did not move");
                verify();s=Sketch::from_serialized(s.serialized());verify();
            }
            if(full&&driving&&locked) {
                // The angular seed fits collinearity; the ordinary final
                // solve must still enforce this native segment's finite domain.
                auto finite=s;const auto first=*finite.find_point(A),last=*finite.find_point(C);
                const auto position=[&](double f){return std::array{first.x+f*(last.x-first.x),first.y+f*(last.y-first.y)};};
                const auto mid=position(.5);const auto contact=finite.add_point(mid[0],mid[1],1e-8,true);
                static_cast<void>(finite.add_point_on_line_constraint(contact,slope));
                auto coordinate=finite.create_axis_dimension(contact,swap?"sketch_axis:y":"sketch_axis:x");finite.apply_dimension(coordinate);
                for(double f:{.55,.45,.5}) {
                    const auto expected=position(f);check(finite.set_dimension_value(coordinate.id,expected[swap?0:1]),"Finite angular contact edit rejected");
                    const auto* actual=finite.find_point(contact);if(std::hypot(actual->x-expected[0],actual->y-expected[1])>=1e-7)
                        throw std::runtime_error("Finite angular contact equation: actual="+std::to_string(actual->x)+","+std::to_string(actual->y)+" expected="+std::to_string(expected[0])+","+std::to_string(expected[1]));
                    const auto* line_start=finite.find_point(A);const auto* line_end=finite.find_point(C);
                    check(std::abs((actual->x-line_start->x)*(line_end->y-line_start->y)-(actual->y-line_start->y)*(line_end->x-line_start->x))/std::hypot(line_end->x-line_start->x,line_end->y-line_start->y)<1e-8,
                        "Finite angular contact left its independent line equation");
                }
                const auto before=finite.serialized();const auto beyond=position(1.1);
                check(!finite.set_dimension_value(coordinate.id,beyond[swap?0:1])&&finite.serialized()==before,"Angular seed accepted contact beyond finite support");
            }
            ++cases;
        }catch(const std::exception& error){throw std::runtime_error("Triangle swap="+std::to_string(swap)+" sx="+std::to_string(sx)+" sy="+std::to_string(sy)+" reverse="+std::to_string(reverse)+" external="+std::to_string(external)+" locked="+std::to_string(locked)+" full="+std::to_string(full)+" driving="+std::to_string(driving)+" driver="+std::to_string(driver)+": "+error.what());}
    }
    std::cout<<cases<<" angled triangle variants: successive coordinate/angle edits, actual equations, ordered participants, native/external anchors, lock/reference/DOF states, dragging and serialized reopen passed\n";
}
