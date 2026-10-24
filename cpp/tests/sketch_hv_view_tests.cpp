#include <zima/document/document_session.hpp>
#include <zima/viewer/sketch_hv.hpp>
#include "../app/sketch_point_pick_priority.hpp"
#include <QQuaternion>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QVector3D vector(kernel::Vec3 p){return {float(p.x),float(p.y),float(p.z)};}
}
int main() {
 try {
    unsigned cases{};
    using K=sketcher::ConstraintKind;
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(bool back:{false,true})for(int turn=0;turn<4;++turn)
    for(bool screen_vertical:{false,true})for(bool external:{false,true})for(bool reverse:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        auto sketch=sketcher::Sketch::create_default();sketch.plane=plane;sketch.refresh_default_frame();
        const auto align=QQuaternion::fromAxes(vector(sketch.x_axis()),vector(sketch.y_axis()),vector(sketch.normal())).conjugated();
        const auto q=QQuaternion::fromAxisAndAngle(0,0,1,float(turn*90))*
            (back?QQuaternion::fromAxisAndAngle(1,0,0,180):QQuaternion{})*align;
        const auto x=q.rotatedVector(vector(sketch.x_axis())),y=q.rotatedVector(vector(sketch.y_axis()));
        const bool swapped=viewer::sketch_hv_swap_for_view(x,y,false);
        check(swapped==(turn%2!=0),"Quarter-turn H/V mapping changed with plane or front/back side");
        const auto oblique=QQuaternion::fromAxisAndAngle(0,1,0,37);
        check(viewer::sketch_hv_swap_for_view(oblique.rotatedVector(x),oblique.rotatedVector(y),swapped)==swapped,
            "Oblique navigation discarded the last perpendicular assignment");
        const bool native_vertical=screen_vertical!=swapped;
        const auto kind=native_vertical?K::Vertical:K::Horizontal;
        const auto line=sketch.add_segment(2,3,12,7);
        const auto first=sketch.segments.front().first_point_id,last=sketch.segments.front().second_point_id;
        sketch.set_point_fixed(first,true);
        std::string support=first;
        if(external) {
            auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
            reference.source_document_id="source";reference.source_owner_id="source-point";reference.source_semantic_key="point";
            reference.cached_points={{2,3}};support=reference.id;sketch.add_external_reference(reference);
        }
        static_cast<void>(sketch.add_point_pair_constraint(reverse?last:support,reverse?support:last,kind));
        auto dimension=sketch.create_segment_dimension(line);dimension.driving=driving;dimension.locked=locked;
        if(!driving&&locked) {
            const auto before=sketch.serialized();bool rejected=false;
            try{sketch.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&sketch.serialized()==before,"A locked reference dimension was accepted or mutated the Sketch");
            dimension.locked=false;
        }
        sketch.apply_dimension(dimension);
        const auto sources=sketch.external_references;
        const auto equation=[&] {
            const auto *a=sketch.find_point(first),*b=sketch.find_point(last);
            check(std::abs(native_vertical?b->x-a->x:b->y-a->y)<1e-8,"H/V native equation failed after an edit");
            const auto world=q.rotatedVector(vector(sketch.world_point(b->x,b->y))-vector(sketch.world_point(a->x,a->y)));
            check(std::abs(screen_vertical?world.x():world.y())<1e-5,"Native H/V does not match screen intent");
            check(sketch.external_references==sources,"H/V edit changed its read-only source");
            if(driving)check(std::abs(std::hypot(b->x-a->x,b->y-a->y)-sketch.dimensions.front().value)<1e-8,"Driving length equation failed");
        };
        equation();
        for(double value:{13.,5.,9.}) {
            const auto before=sketch.serialized();static_cast<void>(sketch.set_dimension_value(dimension.id,value));
            if(!driving)check(before==sketch.serialized(),"Reference dimension drove H/V geometry");
            equation();
        }
        for(double value:{16.,22.,11.}) {
            const auto before=sketch.serialized();const auto original=*sketch.find_point(last);
            const bool moved=sketch.move_point(last,native_vertical?2:value,native_vertical?value:3);
            if(driving&&locked) {
                check(sketch.serialized()==before,"Locked fully constrained H/V changed while dragged");
            }
            else check(moved&&std::abs(native_vertical?sketch.find_point(last)->y-value:sketch.find_point(last)->x-value)<1e-8,"Free H/V endpoint failed repeated dragging");
            equation();
        }
        const auto saved=sketch.serialized();check(sketcher::Sketch::from_serialized(saved).serialized()==saved,"H/V identity changed after reopening");
        if(driving) {
            bool rejected=false;try {static_cast<void>(sketch.add_segment_constraint(line,native_vertical?K::Horizontal:K::Vertical));}catch(const std::exception&){rejected=true;}
            check(rejected&&sketch.serialized()==saved,"Impossible orthogonal relation changed a length-constrained H/V segment");
        }
        ++cases;
    }
    for(const auto native:{viewer::CandidateKind::SketchPoint,viewer::CandidateKind::SketchSegment,viewer::CandidateKind::SketchCurve}) {
        viewer::ViewerCandidate a,b;a.kind=native;b.kind=viewer::CandidateKind::SketchAxis;
        check(app::sketch_edit_pick_priority(a)<app::sketch_edit_pick_priority(b),"Axes outrank authored Sketch geometry");
        b.kind=viewer::CandidateKind::SketchExternalReference;
        check(app::sketch_edit_pick_priority(a)<app::sketch_edit_pick_priority(b),"References outrank authored Sketch geometry");
    }
    std::cout<<cases<<" view-relative H/V equation, dimension, drag and persistence cases passed\n";
    return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
