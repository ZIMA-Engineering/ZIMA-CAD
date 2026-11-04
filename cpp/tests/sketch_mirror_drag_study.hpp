#pragma once
#include <zima/document/part_document.hpp>
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

inline int run_sketch_mirror_drag_study() {
    using namespace zima::sketcher;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    unsigned cases{};
    for(bool horizontal:{false,true})for(bool reflected:{false,true})
    for(bool reverse:{false,true})for(bool diameter:{false,true})for(int state=0;state<3;++state) {
        auto sketch=Sketch::create_default();
        const auto source=sketch.add_circle(horizontal?0:20,horizontal?20:0,3);
        const auto center=sketch.circles.front().center_point_id;
        static_cast<void>(sketch.add_point_on_line_constraint(center,
            horizontal?"sketch_axis:y":"sketch_axis:x"));
        const auto copy=sketch.mirror_geometry({source},horizontal?"sketch_axis:x":"sketch_axis:y");
        const auto mirror=std::ranges::find(sketch.circles,copy.geometry_ids.front(),&SketchCircle::id)->center_point_id;
        if(reverse)for(auto& c:sketch.constraints)if(c.kind==ConstraintKind::Symmetric)
            std::swap(c.first_point_id,c.second_point_id);
        const auto curve=reflected?copy.geometry_ids.front():source;
        auto size=diameter?sketch.create_circle_diameter_dimension(curve):sketch.create_circle_radius_dimension(curve);
        size.driving=state!=0;size.locked=state==2;sketch.apply_dimension(size);
        const auto id=reflected?mirror:center;
        const auto equations=[&] {
            const auto *a=sketch.find_point(center),*b=sketch.find_point(mirror);
            check(a&&b,"Mirror center identity was lost");
            check(std::abs(horizontal?a->x-b->x:a->y-b->y)<1e-7&&
                std::abs(horizontal?a->y+b->y:a->x+b->x)<1e-7&&
                std::abs(horizontal?a->x:a->y)<1e-7,"Mirror drag violated symmetry or its source axis");
            check(std::abs(sketch.circles[0].radius-sketch.circles[1].radius)<1e-7,
                "Mirror drag violated equal radii");
        };
        for(double distance:{25.,18.,22.}) {
            const double target=reflected?-distance:distance;
            check(sketch.move_point(id,horizontal?.4:target,horizontal?target:.4),
                "Off-axis cursor blocked a movable mirror center");
            const auto* moved=sketch.find_point(id);
            check(std::abs((horizontal?moved->y:moved->x)-target)<1e-7,
                "Mirror center did not follow the permitted cursor component");
            equations();
            sketch=Sketch::from_serialized(sketch.serialized());equations();
        }
        if(size.driving)for(double radius:{4.,2.5,3.}) {
            check(sketch.set_dimension_value(size.id,diameter?2*radius:radius),"Mirror radius edit failed after a center drag");
            equations();check(std::abs(sketch.circles.front().radius-radius)<1e-7,"Mirror radius did not reach both circles");
        }
        auto span=sketch.create_point_dimension(center,mirror,horizontal?DimensionKind::DistanceY:DimensionKind::DistanceX);
        span.locked=true;sketch.apply_dimension(span);
        const auto before=sketch.serialized();
        check(!sketch.move_point(id,horizontal?.4:30.,horizontal?30.:.4)&&sketch.serialized()==before,
            "Fully constrained mirror center escaped or changed on rejection");
        auto unlocked=span;unlocked.locked=false;sketch.apply_dimension(unlocked);
        check(sketch.move_point(id,horizontal?.4:30.,horizontal?30.:.4),"Unlocked mirror span blocked dragging");
        equations();++cases;
    }
    if(const auto* file=std::getenv("ZIMA_CPP_VERIFY_MIRROR_DRAG_DOCUMENT")) {
        const auto part=zima::document::PartDocument::load(std::filesystem::u8path(file));
        const auto feature=std::ranges::find(part.history,std::string("Tažení 03"),&zima::document::HistoryContainer::name);
        check(feature!=part.history.end(),"Supplied document has no Tažení 03");
        const auto found=std::ranges::find(part.sketches,feature->id,&Sketch::owner_container_id);
        check(found!=part.sketches.end()&&found->circles.size()==2,"Supplied profile has no symmetric hole pair");
        for(const auto& circle:found->circles) {
            auto sketch=*found;
            const auto* initial=sketch.find_point(circle.center_point_id);
            const double sign=std::copysign(1.,initial->y);
            for(double distance:{250.,225.,245.}) {
                check(sketch.move_point(circle.center_point_id,.4,sign*distance),
                    "Supplied Tažení 03 rejected an off-axis center drag");
                const auto *a=sketch.find_point(found->circles[0].center_point_id),
                    *b=sketch.find_point(found->circles[1].center_point_id);
                check(std::abs(a->x)<1e-7&&std::abs(b->x)<1e-7&&std::abs(a->y+b->y)<1e-7&&
                    std::abs(std::abs(a->y)-distance)<1e-7,"Supplied profile drag violated its equations");
            }
        }
    }
    std::cout<<"Mirror center drag: "<<cases<<" base-axis/participant/order/radius/diameter/driving/reference/lock cases passed\n";
    return 0;
}
