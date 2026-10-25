#include <zima/sketcher/sketch.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <cmath>
#include <algorithm>
namespace {
using namespace zima::sketcher;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Sketch fixture(bool external,double offset,double side) {
    auto s=Sketch::create_default();
    const double bx=31,by=33,ex=1,ey=33;
    const double t=(bx*ex+by*ey)/(ex*ex+ey*ey);
    static_cast<void>(s.add_segment(offset+2*t*ex-bx,side*(2*t*ey-by),offset,0));
    static_cast<void>(s.add_segment(offset,0,offset+bx,side*by));
    const auto axis=s.add_segment(offset,0,offset+ex,side*ey,1e-6,true);
    static_cast<void>(s.add_segment_pair_constraint(s.segments[0].id,s.segments[1].id,ConstraintKind::EqualLength));
    static_cast<void>(s.add_point_pair_constraint(s.segments[1].second_point_id,s.segments[2].second_point_id,ConstraintKind::Horizontal));
    static_cast<void>(s.add_symmetric_constraint(s.segments[0].first_point_id,s.segments[1].second_point_id,axis));
    std::string support;
    if(external) {
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Face);
        ref.source_document_id="read-only-source";ref.source_owner_id="plane-owner";ref.source_semantic_key="plane";
        ref.infinite=true;ref.cached_points={{offset-100,-side*5},{offset+100,-side*5}};
        support=ref.id;s.add_external_reference(ref);
    } else {
        support=s.add_segment(offset-100,-side*5,offset+100,-side*5,1e-6,true);
        s.set_point_fixed(s.segments.back().first_point_id,true);s.set_point_fixed(s.segments.back().second_point_id,true);
    }
    s.apply_dimension(s.create_point_line_dimension(s.segments[0].second_point_id,support));
    return s;
}
void equations(const Sketch& s,bool perpendicular,bool vertical) {
    const auto *a=s.find_point(s.segments[0].first_point_id),*j=s.find_point(s.segments[0].second_point_id),
        *b=s.find_point(s.segments[1].second_point_id),*e=s.find_point(s.segments[2].second_point_id);
    const double ax=a->x-j->x,ay=a->y-j->y,bx=b->x-j->x,by=b->y-j->y,ex=e->x-j->x,ey=e->y-j->y;
    check(std::abs(std::hypot(ax,ay)-std::hypot(bx,by))<1e-7,"Equal legs changed");
    const double t=(ax*ex+ay*ey)/(ex*ex+ey*ey);
    check(std::hypot(2*t*ex-ax-bx,2*t*ey-ay-by)<1e-7,"Symmetry equation changed");
    check(std::abs(b->y-e->y)<1e-7,"Existing point alignment changed");
    if(perpendicular)check(std::abs((ax*bx+ay*by)/(std::hypot(ax,ay)*std::hypot(bx,by)))<1e-8,"Perpendicular equation changed");
    if(vertical)check(std::abs(ex)<1e-7,"Axis point vertical equation changed");
    for(const auto& d:s.dimensions)if(!d.suppressed) {
        if(d.kind==DimensionKind::DistancePointLine) {
            std::array<double,2> origin,direction;
            if(const auto ref=std::ranges::find(s.external_references,d.geometry_id,&SketchExternalReference::id);ref!=s.external_references.end()) {
                origin=ref->cached_points.front();direction={ref->cached_points.back()[0]-origin[0],ref->cached_points.back()[1]-origin[1]};
            } else {
                const auto segment=std::ranges::find(s.segments,d.geometry_id,&SketchSegment::id);
                const auto *p=s.find_point(segment->first_point_id),*q=s.find_point(segment->second_point_id);
                origin={p->x,p->y};direction={q->x-p->x,q->y-p->y};
            }
            const auto* p=s.find_point(d.first_point_id);
            const double signed_distance=((p->x-origin[0])*direction[1]-(p->y-origin[1])*direction[0])/std::hypot(direction[0],direction[1]);
            check(std::abs(std::abs(signed_distance)-std::abs(d.value))<1e-7,"Support distance equation changed");
        } else if(d.kind==DimensionKind::Distance||d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY) {
            const auto *p=s.find_point(d.first_point_id),*q=s.find_point(d.second_point_id);
            if(!p||!q)continue;
            const double measured=d.kind==DimensionKind::Distance?std::hypot(q->x-p->x,q->y-p->y):
                d.kind==DimensionKind::DistanceX?q->x-p->x:q->y-p->y;
            check(std::abs(measured-d.value)<1e-7,"Size dimension equation changed");
        }
    }
}
void exercise(Sketch s,bool reverse,bool vertical_first,DimensionKind dimension_kind,bool driving,bool locked) {
    const auto sources=s.external_references;const auto count=s.points.size();
    const auto junction=s.segments[0].second_point_id,tip=s.segments[1].second_point_id,axis_tip=s.segments[2].second_point_id;
    check(s.solve().remaining_degrees_of_freedom==4,"Dependent equal-length row invented a degree-of-freedom restriction");
    for(bool vertical:{vertical_first,!vertical_first}) {
        if(vertical)static_cast<void>(s.add_point_pair_constraint(reverse?axis_tip:junction,reverse?junction:axis_tip,ConstraintKind::Vertical));
        else static_cast<void>(s.add_segment_pair_constraint(s.segments[reverse?1:0].id,s.segments[reverse?0:1].id,ConstraintKind::Perpendicular));
        equations(s,!vertical||vertical!=vertical_first,vertical||vertical!=vertical_first);
    }
    auto size=dimension_kind==DimensionKind::Distance?s.create_segment_dimension(s.segments[1].id):
        s.create_point_dimension(junction,dimension_kind==DimensionKind::DistanceX?tip:axis_tip,dimension_kind);
    size.driving=driving;size.locked=locked;
    if(!driving&&locked) {
        const auto before=s.serialized();bool rejected=false;try{s.apply_dimension(size);}catch(const std::exception&){rejected=true;}
        check(rejected&&s.serialized()==before,"Locked reference dimension was not rejected atomically");size.locked=false;
    }
    s.apply_dimension(size);
    equations(s,true,true);
    if(driving)for(double multiplier:{1.1,.9,1.2}) {
        check(s.set_dimension_value(size.id,dimension_display_value(size,s.plane)*multiplier),"Successive junction size edit failed");equations(s,true,true);
    }
    if(driving&&locked) {
        auto position=s.create_point_dimension("sketch_origin",junction,DimensionKind::DistanceX);position.locked=true;s.apply_dimension(position);
        for(auto& d:s.dimensions)if(d.driving)d.locked=true;
        check(s.solve().remaining_degrees_of_freedom==0,"Locked junction fixture is not fully constrained");
    }
    for(double displacement:{2.,-3.,4.}) {
        const auto old=*s.find_point(tip);const auto before=s.serialized();
        const double dy=std::copysign(std::abs(displacement),old.y-s.find_point(junction)->y);
        const bool moved=s.move_point(tip,old.x+displacement,old.y+dy);
        equations(s,true,true);
        if(driving&&locked)check(s.serialized()==before,"Fully constrained junction moved while dragged");
        if(!driving||!locked)check(moved&&std::hypot(s.find_point(tip)->x-old.x,s.find_point(tip)->y-old.y)>1e-7,"Free symmetric junction did not move");
    }
    check(s.external_references==sources&&s.points.size()==count,"Junction edit changed read-only sources or native incidence");
    auto reopened=Sketch::from_serialized(s.serialized());equations(reopened,true,true);
    check(reopened.serialized()==s.serialized(),"Junction reopen changed identities or definitions");
    const auto before=s.serialized();bool rejected=false;
    try{static_cast<void>(s.add_segment_pair_constraint(s.segments[0].id,s.segments[1].id,ConstraintKind::Parallel));}catch(const std::exception&){rejected=true;}
    check(rejected&&s.serialized()==before,"Impossible parallel constraint changed the perpendicular junction");
}
}
int main(int argc,char** argv) {
    using namespace zima::sketcher;
    try {
        if(argc==1) {
            unsigned cases{};
            for(bool external:{false,true})for(double offset:{0.,-235.})for(double side:{-1.,1.})
            for(bool reverse:{false,true})for(bool vertical_first:{false,true})
            for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})
            for(bool driving:{false,true})for(bool locked:{false,true}) {
                try{exercise(fixture(external,offset,side),reverse,vertical_first,kind,driving,locked);}
                catch(const std::exception& e){std::cerr<<"case="<<cases<<" external="<<external<<" offset="<<offset<<" side="<<side<<" reverse="<<reverse<<" vertical_first="<<vertical_first<<" kind="<<int(kind)<<" driving="<<driving<<" locked="<<locked<<" "<<e.what()<<std::endl;throw;}
                ++cases;
            }
            std::cout<<cases<<" symmetric junction dimension/edit/drag/native support/order/side/reopen cases passed\n";return 0;
        }
        std::ifstream stream(std::filesystem::u8path(argv[1]));
        const std::string text{std::istreambuf_iterator<char>{stream},{}};
        const auto source=Sketch::from_serialized(text);
        {auto copy=source;const auto status=copy.solve();std::cout<<"baseline status="<<int(status.status)<<" dof="<<status.remaining_degrees_of_freedom<<" residual="<<status.maximum_residual<<std::endl;}
        bool passed=true;
        for(bool reverse:{false,true})for(bool vertical:{false,true}) {
            auto sketch=source;const auto before=sketch.serialized();
            try {
                if(vertical) {
                    const auto& axis=sketch.segments.at(2);
                    static_cast<void>(sketch.add_point_pair_constraint(reverse?axis.second_point_id:axis.first_point_id,
                        reverse?axis.first_point_id:axis.second_point_id,ConstraintKind::Vertical));
                } else static_cast<void>(sketch.add_segment_pair_constraint(sketch.segments.at(reverse?1:0).id,
                    sketch.segments.at(reverse?0:1).id,ConstraintKind::Perpendicular));
                std::cout<<"junction reverse="<<reverse<<" vertical="<<vertical<<" passed\n";
            } catch(const std::exception& e) {
                std::cout<<"junction reverse="<<reverse<<" vertical="<<vertical<<" error="<<e.what()
                    <<" atomic="<<(sketch.serialized()==before)<<std::endl;passed=false;
                auto raw=source;SketchConstraint c;c.id="diagnostic";
                if(vertical) {c.kind=ConstraintKind::Vertical;c.first_point_id=raw.segments.at(2).first_point_id;c.second_point_id=raw.segments.at(2).second_point_id;}
                else {c.kind=ConstraintKind::Perpendicular;c.geometry_id=raw.segments.at(reverse?1:0).id;c.second_geometry_id=raw.segments.at(reverse?0:1).id;}
                raw.constraints.push_back(c);const auto status=raw.solve(2000);
                std::cout<<"raw status="<<int(status.status)<<" dof="<<status.remaining_degrees_of_freedom<<" residual="<<status.maximum_residual<<std::endl;
            }
        }
        return passed?0:1;
    } catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 1;}
}
