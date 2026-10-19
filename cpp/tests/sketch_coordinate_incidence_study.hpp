#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

inline void run_external_coordinate_incidence_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(int kind=0;kind<3;++kind)for(int axis=0;axis<2;++axis)
    for(double sign:{-1.,1.})for(bool reverse:{false,true})for(bool locked:{false,true}) {
        auto sketch=Sketch::create_default();
        const auto position=[&](double x,double y){return axis==0?std::array{sign*x,y}:std::array{y,sign*x};};
        auto support=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        support.source_document_id="read-only-source";support.source_owner_id="support";support.source_semantic_key="curve";
        if(kind==0)support.cached_points={position(2,2),position(6,8)};
        else for(int sample=0;sample<=32;++sample) {
            const double angle=(kind==1?2.:1.)*std::numbers::pi*sample/32;
            support.cached_points.push_back(position(10+5*std::cos(angle),5+5*std::sin(angle)));
        }
        if(reverse)std::ranges::reverse(support.cached_points);
        sketch.add_external_reference(support);const auto initial=kind==0?position(4,5):position(13,9);
        const auto point=sketch.add_point(initial[0],initial[1],1e-9,true);
        if(kind==0)static_cast<void>(sketch.add_point_on_line_constraint(point,support.id));
        else static_cast<void>(sketch.add_point_on_circle_constraint(point,support.id));
        auto dimension=sketch.create_axis_dimension(point,axis==0?"sketch_axis:y":"sketch_axis:x");
        dimension.locked=locked;sketch.apply_dimension(dimension);
        const auto verify=[&] {
            auto solved=sketch;const auto status=solved.solve();
            check(status.maximum_residual<1e-8&&status.status!=SolveStatus::Conflicting,"External coordinate incidence did not solve");
            check(sketch.external_references==std::vector{support},"Coordinate edit changed an external source");
            const auto* p=sketch.find_point(point);check(p,"Coordinate edit lost native point identity");
            const auto actual=axis==0?std::array{p->x/sign,p->y}:std::array{p->y/sign,p->x};
            if(kind==0)check(std::abs((actual[0]-2)*6-(actual[1]-2)*4)<1e-7,"External line equation changed");
            else check(std::abs(std::hypot(actual[0]-10,actual[1]-5)-5)<1e-8,"External circular equation changed");
        };
        verify();
        for(double delta:{.1,.2,0.}) {
            dimension.value=initial[axis]+sign*delta;sketch.apply_dimension(dimension);verify();
            const auto* p=sketch.find_point(point);check(std::abs((axis==0?p->x:p->y)-dimension.value)<1e-8,"External coordinate edit did not reach its value");
        }
        for(double parameter:{.4,.6,.45}) {
            const auto requested=kind==0?position(2+4*parameter,2+6*parameter):
                position(10+5*std::cos(parameter*std::numbers::pi),5+5*std::sin(parameter*std::numbers::pi));
            const auto before=sketch.serialized();const auto previous=*sketch.find_point(point);
            const bool moved=sketch.move_point(point,requested[0],requested[1]);verify();
            if(locked) {
                if(moved||sketch.serialized()!=before)throw std::runtime_error(
                    "Locked external contact changed: kind="+std::to_string(kind)+" axis="+std::to_string(axis)+
                    " sign="+std::to_string(sign)+" parameter="+std::to_string(parameter));
            }
            else if(!moved||std::hypot(sketch.find_point(point)->x-previous.x,sketch.find_point(point)->y-previous.y)<=1e-6)
                throw std::runtime_error("Free external contact did not move: kind="+std::to_string(kind)+" axis="+std::to_string(axis)+
                    " sign="+std::to_string(sign)+" parameter="+std::to_string(parameter));
        }
        sketch=Sketch::from_serialized(sketch.serialized());verify();
    }
}
