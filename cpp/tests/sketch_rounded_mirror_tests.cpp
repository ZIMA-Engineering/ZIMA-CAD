#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

using namespace zima;
using namespace sketcher;
namespace {
std::string context;
void check(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
void solved(Sketch& s) {const auto r=s.solve();check(r.status!=SolveStatus::Conflicting&&r.status!=SolveStatus::Invalid,"Rounded mirror solve failed");}
std::vector<std::string> selection(const Sketch& s,bool corners) {
    std::vector<std::string> result;
    for(const auto& line:s.segments)if(!line.construction)result.push_back(line.id);
    if(corners)for(const auto& corner:s.corner_radii)result.push_back(corner.id);
    return result;
}
void equations(const Sketch& s,const std::string& axis) {
    std::array<double,2> origin{},direction{};
    if(axis=="sketch_axis:x")direction={1,0};
    else if(axis=="sketch_axis:y")direction={0,1};
    else if(const auto r=std::ranges::find(s.external_references,axis,&SketchExternalReference::id);r!=s.external_references.end()) {
        origin=r->cached_points[0];direction={r->cached_points[1][0]-origin[0],r->cached_points[1][1]-origin[1]};
    } else {
        const auto l=std::ranges::find(s.segments,axis,&SketchSegment::id);check(l!=s.segments.end(),"Mirror axis missing");
        const auto a=s.find_point(l->first_point_id),b=s.find_point(l->second_point_id);
        origin={a->x,a->y};direction={b->x-a->x,b->y-a->y};
    }
    for(const auto& c:s.constraints)if(!c.suppressed&&c.kind==ConstraintKind::Symmetric&&c.geometry_id==axis) {
        const auto a=s.find_point(c.first_point_id),b=s.find_point(c.second_point_id);check(a&&b,"Symmetry lost an authored point");
        const double t=((a->x-origin[0])*direction[0]+(a->y-origin[1])*direction[1])/(direction[0]*direction[0]+direction[1]*direction[1]);
        check(std::hypot(b->x-(2*(origin[0]+t*direction[0])-a->x),b->y-(2*(origin[1]+t*direction[1])-a->y))<1e-6,"Independent reflected point equation failed");
    }
    for(const auto& c:s.constraints)if(!c.suppressed&&c.kind==ConstraintKind::EqualRadius) {
        const auto a=std::ranges::find(s.corner_radii,c.geometry_id,&SketchCornerRadius::id),b=std::ranges::find(s.corner_radii,c.second_geometry_id,&SketchCornerRadius::id);
        if(a!=s.corner_radii.end()&&b!=s.corner_radii.end())check(std::abs(a->radius-b->radius)<1e-6,"Mirrored corner radius equality failed");
    }
    const auto evaluated=s.evaluated_profile_sketch();evaluated.validate();
    for(const auto& d:s.dimensions)if(!d.suppressed) {
        double actual{};
        if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter) {
            const auto c=std::ranges::find(s.corner_radii,d.geometry_id,&SketchCornerRadius::id);check(c!=s.corner_radii.end(),"Radius driver lost corner");actual=c->radius*(d.kind==DimensionKind::Diameter?2:1);
        } else {
            auto first=d.first_point_id,second=d.second_point_id;
            if(const auto leg=std::ranges::find(evaluated.segments,d.geometry_id,&SketchSegment::id);leg!=evaluated.segments.end()) {first=leg->first_point_id;second=leg->second_point_id;}
            const auto a=evaluated.find_point(first),b=evaluated.find_point(second);check(a&&b,"Dimension lost native endpoints");
            const double dx=b->x-a->x,dy=b->y-a->y;
            if(d.kind==DimensionKind::Distance)actual=std::hypot(dx,dy);
            else if(d.kind==DimensionKind::DistanceX)actual=dx;
            else if(d.kind==DimensionKind::DistanceY)actual=dy;
            else if(d.kind==DimensionKind::Angle){check(std::abs(std::sin(std::atan2(dy,dx)-d.value*std::acos(-1)/180))<1e-6,"Mirror segment angle equation failed");continue;}
            else continue;
        }
        if(std::abs(actual-d.value)>=1e-5)throw std::runtime_error("Rounded mirror dimension equation failed: kind "+std::to_string(static_cast<int>(d.kind))+" actual "+std::to_string(actual)+" value "+std::to_string(d.value));
    }
    for(const auto& corner:s.corner_radii)if(!corner.suppressed&&corner.radius>1e-9) {
        const auto arc=std::ranges::find(evaluated.arcs,corner.id,&SketchArc::id);check(arc!=evaluated.arcs.end(),"Mirrored corner has no exact evaluated arc");
        const auto center=evaluated.find_point(arc->center_point_id);
        for(const auto& leg:{corner.first_segment_id,corner.second_segment_id}) {
            const auto line=std::ranges::find(evaluated.segments,leg,&SketchSegment::id);check(line!=evaluated.segments.end(),"Rounded mirror lost supporting leg");
            const auto a=evaluated.find_point(line->first_point_id),b=evaluated.find_point(line->second_point_id);
            const double dx=b->x-a->x,dy=b->y-a->y,length=std::hypot(dx,dy);
            check(length>1e-8&&std::abs(std::abs((center->x-a->x)*dy-(center->y-a->y)*dx)/length-corner.radius)<1e-6,"Rounded mirror tangent equation failed");
        }
    }
}
Sketch fixture() {
    auto s=Sketch::create_default();
    const auto a=s.add_segment(12,10,42,10),b=s.add_segment(42,10,42,50),c=s.add_segment(42,50,12,50),d=s.add_segment(12,50,12,10);
    static_cast<void>(s.add_corner_fillet(a,b,3));static_cast<void>(s.add_corner_fillet(b,c,3));
    static_cast<void>(s.add_corner_fillet(c,d,3));static_cast<void>(s.add_corner_fillet(d,a,3));return s;
}
void actual(const std::filesystem::path& path,bool reproduce) {
    auto doc=document::PartDocument::load(path);check(doc.sketches.size()==1,"Actual skeleton Sketch changed");
    auto& s=doc.sketches.front();check(s.corner_radii.size()==4,"Actual skeleton lacks four rounded corners");
    if(reproduce) {
        const auto unchanged=s.serialized();bool rejected=false;
        try{static_cast<void>(s.mirror_geometry(selection(s,true),"sketch_axis:y"));}catch(const std::exception& e){std::cout<<"Reproduced actual rounded mirror rejection: "<<e.what()<<'\n';rejected=true;}
        check(rejected&&s.serialized()==unchanged,"Reported mirror rejection was not reproduced atomically");return;
    }
    for(const auto axis:{"sketch_axis:x","sketch_axis:y"})for(bool corners:{false,true}) {
        auto copy=s;const auto ids=selection(copy,corners);static_cast<void>(copy.mirror_geometry(ids,axis));
        check(copy.corner_radii.size()==8&&copy.segments.size()==8,"Actual skeleton mirror lost rounded corners or legs");
        equations(copy,axis);
        for(double r:{8.,12.,10.}){const auto c=copy.corner_radii.front();static_cast<void>(copy.add_corner_fillet(c.first_segment_id,c.second_segment_id,r));solved(copy);equations(copy,axis);copy=Sketch::from_serialized(copy.serialized());}
    }
    std::cout<<"Actual skeleton: both axes, explicit/implicit corners and repeated radius changes passed\n";
    const auto before=s.serialized();const auto id=s.corner_radii.front().id;
    const auto evaluated=s.evaluated_profile_sketch();
    s.remove_dimension(id);check(!s.corner_radii.front().dimension_visible&&s.corner_radii.size()==4,
        "Removing intrinsic radius annotation removed the rounded corner");
    const auto after=s.evaluated_profile_sketch();
    check(after.segments==evaluated.segments&&after.arcs==evaluated.arcs&&after.points==evaluated.points,
        "Removing intrinsic radius annotation changed visible geometry");
    document::DocumentSession session(document::PartDocument::load(path));
    auto changed=session.document();changed.sketches.front()=s;session.commit(changed);
    check(session.undo()&&session.document().sketches.front().serialized()==before,"Radius annotation Undo lost source state");
    check(session.redo()&&session.document().sketches.front().serialized()==s.serialized(),"Radius annotation Redo lost removal");
    auto loaded=Sketch::from_serialized(s.serialized());check(!loaded.corner_radii.front().dimension_visible,"Removed radius annotation returned after reopen");
    std::cout<<"Actual skeleton intrinsic radius deletion preserved exact geometry; Undo/Redo and native Sketch reopen passed\n";
}
void matrix() {
    unsigned count{};
    for(int axis_kind=0;axis_kind<4;++axis_kind)for(bool reverse:{false,true})for(bool corners:{false,true}) {
        auto base=fixture();std::string axis=axis_kind<2?(axis_kind?"sketch_axis:y":"sketch_axis:x"):
            base.add_segment(axis_kind==2?-20:3,-30,axis_kind==2?20:3,30,1e-6,true);
        auto ids=selection(base,corners);if(reverse)std::ranges::reverse(ids);const auto source_corners=base.corner_radii;
        const auto source_segments=base.segments;static_cast<void>(base.mirror_geometry(ids,axis));
        check(base.corner_radii.size()==8,"Rounded mirror failed to copy all corner records");
        for(std::size_t i=0;i<4;++i)check(base.corner_radii[i]==source_corners[i],"Mirror changed a source corner or its annotation");
        for(std::size_t i=0;i<source_segments.size();++i)check(base.segments[i]==source_segments[i],"Mirror changed a source leg");
        equations(base,axis);const auto sources=base.external_references;
        for(bool copy_driver:{false,true})for(bool diameter:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}) {
            context="radius axis="+std::to_string(axis_kind)+" copy="+std::to_string(copy_driver)+" diameter="+std::to_string(diameter)+" driving="+std::to_string(driving)+" locked="+std::to_string(locked);
            auto s=base;const auto id=s.corner_radii[copy_driver?4:0].id;
            auto d=diameter?s.create_arc_diameter_dimension(id):s.create_arc_radius_dimension(id);d.driving=driving;d.locked=locked;
            if(!driving&&locked){const auto before=s.serialized();bool rejected=false;try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&before==s.serialized(),"Locked reference radius was not rejected atomically");continue;}
            s.apply_dimension(d);
            for(double r:{4.,2.,3.5}) {const auto before=s.serialized();check(s.set_dimension_value(d.id,r*(diameter?2:1)),"Rounded mirror R/D edit failed");if(!driving)check(s.serialized()==before,"Reference radius changed native geometry");solved(s);equations(s,axis);s=Sketch::from_serialized(s.serialized());}
            check(s.external_references==sources,"Rounded mirror radius edit changed external axis");++count;
            for(int i=0;i<3;++i){const auto p=s.points.front();check(s.move_point(p.id,p.x+.2,p.y+.3),"Radius-dimensioned rounded mirror drag rejected");solved(s);equations(s,axis);}
        }
        for(bool copy_driver:{false,true})for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY,DimensionKind::Angle})for(bool locked:{false,true})for(bool driving:{false,true}) {
            context="segment axis="+std::to_string(axis_kind)+" copy="+std::to_string(copy_driver)+" kind="+std::to_string(static_cast<int>(kind))+" driving="+std::to_string(driving)+" locked="+std::to_string(locked);
            auto s=base;const auto segment=s.segments[copy_driver?source_segments.size():0].id;
            // X is nonzero on a horizontal source leg, Y on its vertical neighbour.
            const auto leg=kind==DimensionKind::DistanceY?s.segments[copy_driver?source_segments.size()+1:1].id:segment;
            auto d=s.create_segment_dimension(leg,kind);d.locked=locked;d.driving=driving;
            if(!driving&&locked){const auto before=s.serialized();bool rejected=false;try{s.apply_dimension(d);}catch(const std::exception&){rejected=true;}check(rejected&&before==s.serialized(),"Locked reference segment dimension was not atomic");continue;}
            s.apply_dimension(d);
            for(double factor:{1.03,.97,1.02}) {const auto before=s.serialized();const auto value=kind==DimensionKind::Angle?dimension_display_value(d)+factor:dimension_display_value(d)*factor;const auto accepted=s.set_dimension_value(d.id,value);if(!accepted)check(s.serialized()==before,"Rejected dimension edit changed mirror");else if(!driving)check(s.serialized()==before,"Reference length edit changed mirror");solved(s);equations(s,axis);s=Sketch::from_serialized(s.serialized());}
            check(s.external_references==sources,"Mirror dimension edit changed external axis");++count;
            for(int i=0;i<3;++i){const auto p=s.points.front();const auto before=s.serialized();const auto moved=s.move_point(p.id,p.x+.1,p.y+.15);
                if(!moved)check(s.serialized()==before,"Rejected dimensioned mirror drag changed geometry");
                else check(std::hypot(s.find_point(p.id)->x-p.x,s.find_point(p.id)->y-p.y)>.01,"Dimensioned mirror drag did not move an available DOF");
                solved(s);equations(s,axis);}
        }
        for(bool fixed:{false,true})for(bool copy_drag:{false,true}) {
            auto s=base;if(fixed)for(const auto p:std::vector(s.points))s.set_point_fixed(p.id,true);
            const auto id=s.segments[copy_drag?source_segments.size():0].first_point_id;
            for(int step=0;step<3;++step){const auto before=s.serialized();const auto p=*s.find_point(id);const bool moved=s.move_point(id,p.x+.2,p.y+.3);if(fixed)check(!moved&&s.serialized()==before,"Fixed rounded mirror escaped on drag");else {check(moved,"Free rounded mirror drag rejected");check(std::hypot(s.find_point(id)->x-p.x,s.find_point(id)->y-p.y)>.01,"Free mirror did not move");}solved(s);equations(s,axis);}
            check(s.external_references==sources,"Mirror drag changed external axis");
        }
    }
    std::cout<<count<<" rounded mirror R/D and segment dimension cases; four axis kinds, both selection orders, fixed/free and source/copy drags passed\n";
}
void rectangle_equations(const Sketch& s) {
    check(s.points.size()==4,"Symmetric rectangle lost its native vertices");
    const double x=std::abs(s.points.front().x),y=std::abs(s.points.front().y);
    for(const auto& p:s.points)check(std::abs(std::abs(p.x)-x)<1e-6&&std::abs(std::abs(p.y)-y)<1e-6,"Symmetric rectangle escaped its centered rectangle equations");
    double sx{},sy{};for(const auto& p:s.points){sx+=p.x;sy+=p.y;}check(std::abs(sx)<1e-6&&std::abs(sy)<1e-6,"Symmetric rectangle moved away from its origin");
    equations(s,"sketch_axis:x");equations(s,"sketch_axis:y");
}
void rectangle_matrix(const Sketch& original) {
    unsigned cases{},edits{},drags{};
    for(bool rotate:{false,true})for(bool reverse_support:{false,true})for(bool reverse_order:{false,true}) {
        auto base=original;
        if(rotate) {
            for(auto& p:base.points){const auto x=p.x;p.x=-p.y;p.y=x;}
            for(auto& c:base.constraints) {
                if(c.kind==ConstraintKind::Horizontal)c.kind=ConstraintKind::Vertical;
                else if(c.kind==ConstraintKind::Vertical)c.kind=ConstraintKind::Horizontal;
                else if(c.kind==ConstraintKind::Symmetric)c.geometry_id=c.geometry_id=="sketch_axis:x"?"sketch_axis:y":"sketch_axis:x";
            }
        }
        if(reverse_support)for(auto& c:base.constraints)std::swap(c.first_point_id,c.second_point_id);
        if(reverse_order)std::ranges::reverse(base.constraints);
        for(const auto& point:base.points)for(int dimension_kind=-1;dimension_kind<3;++dimension_kind)
            for(bool driving:{false,true})for(bool locked:{false,true})for(bool fixed:{false,true}) {
                if(dimension_kind<0&&(driving||locked))continue;
                auto s=base;context="rectangle matrix rotate="+std::to_string(rotate)+" reverse="+std::to_string(reverse_support)+std::to_string(reverse_order)+
                    " point="+point.id+" dimension="+std::to_string(dimension_kind)+" driving="+std::to_string(driving)+" locked="+std::to_string(locked)+" fixed="+std::to_string(fixed);
                if(fixed)for(const auto p:std::vector(s.points))s.set_point_fixed(p.id,true);
                bool horizontal=true;
                if(dimension_kind>=0) {
                    horizontal=dimension_kind!=2;const auto segment=std::ranges::find_if(s.segments,[&](const auto& line){
                        const auto a=s.find_point(line.first_point_id),b=s.find_point(line.second_point_id);return horizontal?std::abs(a->y-b->y)<1e-8:std::abs(a->x-b->x)<1e-8;});
                    check(segment!=s.segments.end(),"Rectangle matrix cannot find its dimensioned side");
                    auto d=s.create_segment_dimension(segment->id,dimension_kind==0?DimensionKind::Distance:dimension_kind==1?DimensionKind::DistanceX:DimensionKind::DistanceY);
                    d.driving=driving;d.locked=locked;const auto before=s.serialized();bool applied=true;
                    try{s.apply_dimension(d);}catch(const std::exception&){applied=false;}
                    if((!driving&&locked)||(driving&&fixed)){check(!applied&&before==s.serialized(),"Impossible/redundant rectangle dimension changed the document");++cases;continue;}
                    check(applied,"Valid symmetric rectangle dimension was rejected");
                    for(double factor:{1.1,.9,1.03}) {
                        const auto unchanged=s.serialized();check(s.set_dimension_value(d.id,dimension_display_value(d)*factor),"Symmetric rectangle dimension edit failed");
                        if(!driving)check(s.serialized()==unchanged,"Reference rectangle dimension moved its source");
                        solved(s);rectangle_equations(s);check(s.external_references==base.external_references,"Rectangle dimension changed external references");
                        s=Sketch::from_serialized(s.serialized());++edits;
                    }
                }
                for(int step=0;step<3;++step) {
                    const auto before=s.serialized();const auto p=*s.find_point(point.id);double x=p.x+std::copysign(.2,p.x),y=p.y+std::copysign(.15,p.y);
                    if(dimension_kind>=0&&driving&&locked){if(horizontal)x=p.x;else y=p.y;}
                    const bool moved=s.move_point(point.id,x,y);
                    if(fixed)check(!moved&&before==s.serialized(),"Fully fixed symmetric rectangle escaped on drag");
                    else {check(moved&&std::hypot(s.find_point(point.id)->x-p.x,s.find_point(point.id)->y-p.y)>.01,"Symmetric rectangle rejected an available drag DOF");++drags;}
                    solved(s);rectangle_equations(s);check(s.external_references==base.external_references,"Rectangle drag changed immutable external references");
                    s=Sketch::from_serialized(s.serialized());
                }
                ++cases;
            }
    }
    std::cout<<cases<<" symmetric rectangle states, "<<edits<<" dimension edits and "<<drags<<" DOF drags; all vertices, orientations/orders, driving/reference and fixed/free passed\n";
}
Sketch centered_rectangle_fixture() {
    auto s=Sketch::create_default();static_cast<void>(s.add_rectangle(-12,7,12,-7));
    const auto& first=s.segments[0];const auto second=s.segments[1];const auto a=first.first_point_id,b=first.second_point_id,c=second.second_point_id;
    static_cast<void>(s.add_symmetric_constraint(a,b,"sketch_axis:y"));static_cast<void>(s.add_symmetric_constraint(b,c,"sketch_axis:x"));return s;
}
void actual_rectangle(const std::filesystem::path& path) {
    const auto doc=document::PartDocument::load(path);const auto found=std::ranges::find(doc.sketches,std::string("01a16725ed247f3080fa42688a17c36f"),&Sketch::id);
    check(found!=doc.sketches.end(),"Actual Feature 005 Sketch is missing");const auto& original=*found;
    check(original.points.size()==4&&original.dimensions.empty(),"Actual rectangle fixture no longer has four undimensioned points");
    for(const auto& point:original.points) {
        auto s=original;context="actual rectangle point "+point.id;
        for(double scale:{1.1,.9,1.03}) {
            const auto before=s.serialized();const auto p=*s.find_point(point.id);
            check(s.move_point(point.id,p.x*scale,p.y*scale),"Undimensioned symmetric rectangle point drag was rejected");
            check(before!=s.serialized()&&std::hypot(s.find_point(point.id)->x-p.x,s.find_point(point.id)->y-p.y)>.01,"Rectangle accepted drag without movement");
            solved(s);rectangle_equations(s);check(s.external_references==original.external_references,"Rectangle drag changed read-only external reference data");
            s=Sketch::from_serialized(s.serialized());
        }
        auto changed=doc;*std::ranges::find(changed.sketches,original.id,&Sketch::id)=s;
        document::DocumentSession session(doc);session.commit(changed);
        check(session.undo()&&std::ranges::find(session.document().sketches,original.id,&Sketch::id)->serialized()==original.serialized(),"Actual rectangle drag Undo lost source Sketch");
        check(session.redo()&&std::ranges::find(session.document().sketches,original.id,&Sketch::id)->serialized()==s.serialized(),"Actual rectangle drag Redo lost edited Sketch");
    }
    std::cout<<"Actual Feature 005: all four centered-rectangle points, three successive drags each, exact equations, immutable references, native Sketch readback and Undo/Redo passed\n";
    rectangle_matrix(original);
}
}
int main(int argc,char** argv) {try{if(argc>2&&std::string_view(argv[2])=="--symmetric-rectangle"){actual_rectangle(argv[1]);return 0;}if(argc>1)actual(argv[1],argc>2);if(argc<=2){matrix();rectangle_matrix(centered_rectangle_fixture());}return 0;}catch(const std::exception& e){std::cerr<<context<<": "<<e.what()<<'\n';return 1;}}
