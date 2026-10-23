#pragma once
#include <zima/sketcher/sketch.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void run_sketch_external_dimension_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    unsigned cases{};
    for(const auto support_kind:{std::pair{ExternalReferenceKind::Edge,false},std::pair{ExternalReferenceKind::Axis,false},std::pair{ExternalReferenceKind::Face,false},std::pair{ExternalReferenceKind::Edge,true}})
    for(const double angle:{0.,.7,1.5707963267948966})for(const double side:{-1.,1.})
    for(const bool line:{false,true})for(const bool fixed:{false,true})for(const bool locked:{false,true})for(const bool driving:{false,true}) {
        const auto kind=support_kind.first;
        const auto context=" intersection="+std::to_string(support_kind.second)+" kind="+std::to_string(int(kind))+" angle="+std::to_string(angle)+" side="+std::to_string(side)+" line="+std::to_string(line)+" fixed="+std::to_string(fixed)+" locked="+std::to_string(locked)+" driving="+std::to_string(driving);
        try {
        auto sketch=Sketch::create_default();const double tx=std::cos(angle),ty=std::sin(angle),nx=-ty,ny=tx;
        auto support=Sketch::create_external_reference(kind);support.source_document_id="external-dimension-study";support.source_owner_id="source";support.source_semantic_key="line";support.infinite=true;
        support.cached_points={{{-20*tx,-20*ty}},{{20*tx,20*ty}}};
        if(support_kind.second){support.infinite=false;support.intersection=SketchIntersectionBinding{"source","line","straight-branch"};}
        sketch.add_external_reference(support);
        std::string point,segment,other;
        if(line){segment=sketch.add_segment(2*tx+side*10*nx,2*ty+side*10*ny,8*tx+side*10*nx,8*ty+side*10*ny);point=sketch.segments.front().first_point_id;other=sketch.segments.front().second_point_id;}
        else point=sketch.add_point(2*tx+side*10*nx,2*ty+side*10*ny);
        if(fixed){sketch.set_point_fixed(point,true);if(line)sketch.set_point_fixed(other,true);}
        auto dimension=line?sketch.create_line_pair_dimension(support.id,segment,DimensionKind::DistanceLine):sketch.create_point_line_dimension(point,support.id);
        dimension.locked=locked;dimension.driving=driving;
        if((locked&&!driving)||(fixed&&driving)) {
            const auto unchanged=sketch.serialized();bool rejected=false;
            try{sketch.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&sketch.serialized()==unchanged,"Invalid locked reference or redundant fixed dimension was not rejected atomically");
            ++cases;continue;
        }
        sketch.apply_dimension(dimension);
        const auto sources=sketch.external_references;
        const auto verify=[&] {
            const auto* p=sketch.find_point(point);check(p,"External dimension lost native point");
            const auto d=std::ranges::find(sketch.dimensions,dimension.id,&SketchDimension::id);
            check(d!=sketch.dimensions.end()&&std::abs(std::abs(p->x*nx+p->y*ny)-std::abs(d->value))<1e-7,"External dimension equation differs from geometry");
            check(sketch.external_references==sources,"Dimension mutated a read-only source");
            if(line&&driving){const auto* q=sketch.find_point(other);check(q&&std::abs((q->x-p->x)*nx+(q->y-p->y)*ny)<1e-7,"Driving line distance lost parallelism");}
            auto checked=sketch;check(checked.solve().maximum_residual<1e-7,"External dimension solve residual");
        };
        verify();
        for(const double value:{12.,8.,11.}) {
            const auto unchanged=sketch.serialized();const auto before=*sketch.find_point(point);
            const bool accepted=sketch.set_dimension_value(dimension.id,value);
            if(driving&&!fixed)check(accepted,"Feasible external dimension edit rejected");
            if(driving&&fixed)check(!accepted&&sketch.serialized()==unchanged,"Impossible fixed edit was not atomic");
            if(!driving)check(std::hypot(sketch.find_point(point)->x-before.x,sketch.find_point(point)->y-before.y)<1e-8,"Reference dimension drove geometry");
            verify();
        }
        for(int edit=0;edit<3;++edit) {
            const auto before=*sketch.find_point(point);const auto d=std::ranges::find(sketch.dimensions,dimension.id,&SketchDimension::id);const double old_value=d->value;
            const double normal_drag=driving&&locked?0.:1.;
            static_cast<void>(sketch.move_point(point,before.x+tx+nx*normal_drag,before.y+ty+ny*normal_drag));
            const auto* after=sketch.find_point(point);
            if(fixed)check(std::hypot(after->x-before.x,after->y-before.y)<1e-8,"Fully constrained point escaped");
            else check(std::hypot(after->x-before.x,after->y-before.y)>.1,"Permitted external dimension drag did not move");
            if(driving&&locked)check(std::abs(std::abs(after->x*nx+after->y*ny)-std::abs(old_value))<1e-7,"Locked external distance escaped during drag");
            verify();
        }
        auto reopened=Sketch::from_serialized(sketch.serialized());check(reopened.external_references==sources&&reopened.dimensions==sketch.dimensions&&reopened.points==sketch.points,"External dimension native reopen changed equations or identity");
        ++cases;
        }catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+context);}
    }
    unsigned angular{};
    for(const auto kind:{ExternalReferenceKind::Edge,ExternalReferenceKind::Axis,ExternalReferenceKind::Face})
    for(const double axis:{0.,.7,1.5707963267948966})for(const double side:{-1.,1.})for(const bool locked:{false,true})for(const bool driving:{false,true}) {
        if(locked&&!driving)continue; // Rejection is covered by the matrix above.
        auto sketch=Sketch::create_default();
        auto ref=Sketch::create_external_reference(kind);ref.source_document_id="source";ref.source_owner_id="feature";ref.source_semantic_key="line";ref.infinite=true;
        ref.cached_points={{{-20*std::cos(axis),-20*std::sin(axis)}},{{20*std::cos(axis),20*std::sin(axis)}}};sketch.add_external_reference(ref);
        const double angle=axis+side*.35;
        const auto segment=sketch.add_segment(2,10,2+6*std::cos(angle),10+6*std::sin(angle));
        const auto point=sketch.segments.front().first_point_id,other=sketch.segments.front().second_point_id;
        auto d=sketch.create_line_pair_dimension(ref.id,segment,DimensionKind::AngleBetween);d.locked=locked;d.driving=driving;sketch.apply_dimension(d);
        const auto verify=[&]{const auto* a=sketch.find_point(point);const auto* b=sketch.find_point(other);const double dx=b->x-a->x,dy=b->y-a->y;const double value=std::acos(std::clamp((dx*std::cos(axis)+dy*std::sin(axis))/std::hypot(dx,dy),-1.,1.))*180/std::acos(-1.);const auto current=std::ranges::find(sketch.dimensions,d.id,&SketchDimension::id);check(std::abs(value-std::abs(current->value))<1e-6,"External angle equation differs from geometry");check(sketch.external_references.front()==ref,"External angle changed source");};
        for(const double value:{25.,40.,15.}){check(sketch.set_dimension_value(d.id,value),"External angle value rejected");verify();}
        for(int i=0;i<3;++i){const auto before=*sketch.find_point(point);const auto* b=sketch.find_point(other);const double length=std::hypot(b->x-before.x,b->y-before.y);const double dx=(b->x-before.x)/length,dy=(b->y-before.y)/length;check(sketch.move_point(point,before.x+dx*.5,before.y+dy*.5),"External angle permitted length drag rejected");check(std::hypot(sketch.find_point(point)->x-before.x,sketch.find_point(point)->y-before.y)>.1,"External angle drag did not move");verify();}
        auto reopened=Sketch::from_serialized(sketch.serialized());check(reopened.dimensions==sketch.dimensions&&reopened.external_references==sketch.external_references,"External angle reopen changed references");++angular;
    }
    unsigned point_pairs{};
    for(const auto kind:{ExternalReferenceKind::Point,ExternalReferenceKind::EdgeStart,ExternalReferenceKind::EdgeEnd,ExternalReferenceKind::AxisPoint})
    for(const auto measurement:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})for(const bool reverse:{false,true})for(const bool driving:{false,true}) {
        auto sketch=Sketch::create_default();auto ref=Sketch::create_external_reference(kind);ref.source_document_id="source";ref.source_owner_id="feature";ref.source_semantic_key="point";ref.cached_points={{2,3}};sketch.add_external_reference(ref);
        const auto point=sketch.add_point(12,13);auto d=sketch.create_point_dimension(reverse?ref.id:point,reverse?point:ref.id,measurement);d.driving=driving;sketch.apply_dimension(d);
        const auto verify=[&]{const auto* p=sketch.find_point(point);const double dx=p->x-2,dy=p->y-3;const double value=measurement==DimensionKind::Distance?std::hypot(dx,dy):measurement==DimensionKind::DistanceX?std::abs(dx):std::abs(dy);const auto current=std::ranges::find(sketch.dimensions,d.id,&SketchDimension::id);check(std::abs(value-std::abs(current->value))<1e-7,"External point dimension equation differs");check(sketch.external_references.front()==ref,"External point dimension changed source");};
        for(const double value:{12.,8.,11.}){check(sketch.set_dimension_value(d.id,value),"External point-pair value edit rejected");verify();}
        for(int i=0;i<3;++i){const auto before=*sketch.find_point(point);check(sketch.move_point(point,before.x+.3,before.y+.2),"External point-pair drag rejected");check(std::hypot(sketch.find_point(point)->x-before.x,sketch.find_point(point)->y-before.y)>.1,"External point-pair drag did not move");verify();}
        auto reopened=Sketch::from_serialized(sketch.serialized());check(reopened.dimensions==sketch.dimensions&&reopened.external_references==sketch.external_references,"External point-pair reopen changed identity");++point_pairs;
    }
    std::cout<<"External angular cases="<<angular<<" and point-pair/order/kind cases="<<point_pairs<<" edits, drags, equations and reopen passed\n";
    std::cout<<"External point/line dimensions: "<<cases<<" support/orientation/side/fix/lock/driver cases, successive edits and drags, equations and reopen passed\n";
}
