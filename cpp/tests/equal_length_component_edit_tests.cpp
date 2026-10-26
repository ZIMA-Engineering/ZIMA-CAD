#include <zima/document/document_session.hpp>
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace zima::sketcher;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b){check(std::isfinite(a)&&std::abs(a-b)<1e-6,"Independent equation failed");}
double length(const Sketch& s,const std::string& id){const auto line=std::ranges::find(s.segments,id,&SketchSegment::id);check(line!=s.segments.end(),"Segment identity missing");const auto a=s.find_point(line->first_point_id),b=s.find_point(line->second_point_id);return std::hypot(b->x-a->x,b->y-a->y);}
std::optional<std::pair<std::array<double,2>,std::array<double,2>>> line_support(const Sketch& s,const std::string& id){
    if(id=="sketch_axis:x")return std::pair{std::array{0.,0.},std::array{1.,0.}};
    if(id=="sketch_axis:y")return std::pair{std::array{0.,0.},std::array{0.,1.}};
    const auto r=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
    return r==s.external_references.end()?std::nullopt:external_reference_line(*r);
}
void equations(const Sketch& s){
    for(const auto& c:s.constraints)if(!c.suppressed){
        const auto a=s.find_point(c.first_point_id),b=s.find_point(c.second_point_id);
        if(c.kind==ConstraintKind::Horizontal)near(a->y,b->y);
        else if(c.kind==ConstraintKind::Vertical)near(a->x,b->x);
        else if(c.kind==ConstraintKind::PointOnLine){
            if(c.geometry_id=="sketch_axis:x")near(a->y,0);
            else if(c.geometry_id=="sketch_axis:y")near(a->x,0);
            else {const auto r=std::ranges::find(s.external_references,c.geometry_id,&SketchExternalReference::id);check(r!=s.external_references.end(),"External support missing");const auto p=r->cached_points.front(),q=r->cached_points.back();near(((a->x-p[0])*(q[1]-p[1])-(a->y-p[1])*(q[0]-p[0]))/std::hypot(q[0]-p[0],q[1]-p[1]),0);}
        }else if(c.kind==ConstraintKind::EqualLength)near(length(s,c.geometry_id),length(s,c.second_geometry_id));
        else if(c.kind==ConstraintKind::Midpoint){
            const auto segment=std::ranges::find(s.segments,c.geometry_id,&SketchSegment::id);
            check(a&&segment!=s.segments.end(),"Midpoint relation is incomplete");
            const auto first=s.find_point(segment->first_point_id),second=s.find_point(segment->second_point_id);
            check(first&&second,"Midpoint segment endpoint is missing");
            near(a->x,(first->x+second->x)*.5);near(a->y,(first->y+second->y)*.5);
        }
        else throw std::runtime_error("Unchecked fixture constraint");
    }
    for(const auto& d:s.dimensions)if(!d.suppressed){
        if(d.kind==DimensionKind::DistancePointLine||d.kind==DimensionKind::DistanceLine){
            const auto support=line_support(s,d.geometry_id);check(support.has_value(),"Dimension support missing");
            const auto point_id=d.kind==DimensionKind::DistancePointLine?d.first_point_id:
                std::ranges::find(s.segments,d.second_geometry_id,&SketchSegment::id)->first_point_id;
            const auto p=s.find_point(point_id);check(p!=nullptr,"Driven point missing");
            const double n=std::hypot(support->second[0],support->second[1]);
            const double signed_distance=(support->second[0]*(p->y-support->first[1])-
                support->second[1]*(p->x-support->first[0]))/n;
            near(d.value*d.solution_side,signed_distance);
        }else{
            const auto a=s.find_point(d.first_point_id),b=s.find_point(d.second_point_id);check(a&&b,"Dimension point missing");
            const auto dx=b->x-a->x,dy=b->y-a->y;
            near(d.value,d.kind==DimensionKind::Distance?std::hypot(dx,dy):d.kind==DimensionKind::DistanceX?dx:dy);
        }
    }
}
Sketch variant(const Sketch& source,int sx,int sy,bool reverse,bool external,bool projected,bool driving,bool locked){
    auto s=source;for(auto& p:s.points){p.x*=sx;p.y*=sy;}
    if(external)for(const auto axis:{std::string("sketch_axis:x"),std::string("sketch_axis:y")}){
        auto r=Sketch::create_external_reference(ExternalReferenceKind::Edge);r.source_document_id="read-only-support";r.source_owner_id="origin";r.source_semantic_key=axis;r.cached_points=axis.ends_with("x")?std::vector<std::array<double,2>>{{-100,0},{100,0}}:std::vector<std::array<double,2>>{{0,-100},{0,100}};s.add_external_reference(r);
        for(auto& c:s.constraints)if(c.geometry_id==axis)c.geometry_id=r.id;
    }
    for(std::size_t i=0;i<s.dimensions.size();++i){auto& d=s.dimensions[i];if(projected)d.kind=i==0?DimensionKind::DistanceY:DimensionKind::DistanceX;if(reverse)std::swap(d.first_point_id,d.second_point_id);const auto a=s.find_point(d.first_point_id),b=s.find_point(d.second_point_id);d.value=d.kind==DimensionKind::Distance?std::hypot(b->x-a->x,b->y-a->y):d.kind==DimensionKind::DistanceX?b->x-a->x:b->y-a->y;d.driving=driving;d.locked=locked;}
    if(reverse){std::ranges::reverse(s.constraints);for(auto& c:s.constraints)if(c.kind==ConstraintKind::EqualLength)std::swap(c.geometry_id,c.second_geometry_id);}
    s=Sketch::from_serialized(s.serialized());equations(s);return s;
}
void reject_move(Sketch& s,std::size_t index,double dx,double dy){const auto before=s.serialized();const auto p=s.points[index];check(!s.move_point(p.id,p.x+dx,p.y+dy),"Impossible drag accepted");check(s.serialized()==before,"Rejected drag changed document");equations(s);}
Sketch three_rectangle_external_fixture(bool mirrored,bool reverse_support,bool reverse_equal,bool locate_from_midpoint){
    auto s=Sketch::create_default();const double sx=mirrored?-1.:1.;
    auto horizontal=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    horizontal.source_document_id="external-support";horizontal.source_owner_id="horizontal";horizontal.source_semantic_key="edge:horizontal";
    horizontal.cached_points=reverse_support?std::vector<std::array<double,2>>{{1000,-30},{-1000,-30}}:std::vector<std::array<double,2>>{{-1000,-30},{1000,-30}};
    const auto horizontal_id=horizontal.id;s.add_external_reference(horizontal);
    auto vertical=Sketch::create_external_reference(ExternalReferenceKind::Edge);
    vertical.source_document_id="external-support";vertical.source_owner_id="vertical";vertical.source_semantic_key="edge:vertical";
    const double support_x=-1000*sx;
    vertical.cached_points=reverse_support?std::vector<std::array<double,2>>{{support_x,1000},{support_x,-1000}}:std::vector<std::array<double,2>>{{support_x,-1000},{support_x,1000}};
    const auto vertical_id=vertical.id;s.add_external_reference(vertical);
    const std::array<std::array<double,2>,3> x{{{-420*sx,-360*sx},{360*sx,420*sx},{810*sx,870*sx}}};
    std::array<std::vector<std::string>,3> rectangles;
    for(std::size_t i=0;i<rectangles.size();++i){
        rectangles[i]=s.add_rectangle(x[i][0],-30,x[i][1],0);
        const auto bottom=std::ranges::find(s.segments,rectangles[i][0],&SketchSegment::id);
        const auto right=std::ranges::find(s.segments,rectangles[i][1],&SketchSegment::id);
        SketchConstraint lower{"contact:lower:"+std::to_string(i),ConstraintKind::PointOnLine,bottom->first_point_id};
        lower.geometry_id=horizontal_id;s.constraints.push_back(std::move(lower));
        SketchConstraint upper{"contact:upper:"+std::to_string(i),ConstraintKind::PointOnLine,right->second_point_id};
        upper.geometry_id="sketch_axis:x";s.constraints.push_back(std::move(upper));
    }
    for(std::size_t i=1;i<rectangles.size();++i){
        SketchConstraint equal{"equal:"+std::to_string(i),ConstraintKind::EqualLength};
        equal.geometry_id=reverse_equal?rectangles[i][0]:rectangles[i-1][0];
        equal.second_geometry_id=reverse_equal?rectangles[i-1][0]:rectangles[i][0];
        s.constraints.push_back(std::move(equal));
    }
    const auto first=std::ranges::find(s.segments,rectangles[0][0],&SketchSegment::id);
    const auto second=std::ranges::find(s.segments,rectangles[1][0],&SketchSegment::id);
    const auto third=std::ranges::find(s.segments,rectangles[2][0],&SketchSegment::id);
    const auto first_segment_id=first->id;
    const auto first_second_point_id=first->second_point_id;
    const auto second_first_point_id=second->first_point_id;
    const auto third_first_point_id=third->first_point_id;
    auto first_location_point=first->first_point_id;
    if(locate_from_midpoint){
        first_location_point=s.add_point((x[0][0]+x[0][1])*.5,-30);
        SketchConstraint midpoint{"midpoint:first-lower",ConstraintKind::Midpoint,first_location_point};
        midpoint.geometry_id=first_segment_id;s.constraints.push_back(std::move(midpoint));
    }
    s.dimensions.push_back(s.create_point_line_dimension(first_location_point,vertical_id));
    s.dimensions.push_back(s.create_segment_dimension(first_segment_id));
    s.dimensions.push_back(s.create_point_dimension(first_second_point_id,second_first_point_id,DimensionKind::DistanceX));
    s.dimensions.push_back(s.create_point_line_dimension(third_first_point_id,vertical_id));
    s=Sketch::from_serialized(s.serialized());equations(s);return s;
}
}
int main(){try{
    auto part=zima::document::PartDocument::load("cpp/tests/fixtures/sketch/form-edge-equal-length.prtz");const auto source=part.sketches.back();equations(source);std::size_t edits{},drags{};
    for(int sx:{-1,1})for(int sy:{-1,1})for(bool reverse:{false,true})for(bool external:{false,true})for(bool projected:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}){
        if(!driving&&locked)continue; // Locked reference dimensions are invalid by contract.
        auto base=variant(source,sx,sy,reverse,external,projected,driving,locked);const auto references=base.external_references;
        auto s=base;
        for(const auto& dimension:base.dimensions)for(double factor:{.8,1.2,1.05}){
            const auto before=s.serialized();const bool accepted=s.set_dimension_value(dimension.id,dimension_display_value(dimension,s.plane)*factor);
            if(!accepted)std::cerr<<"case sx="<<sx<<" sy="<<sy<<" reverse="<<reverse<<" external="<<external<<" projected="<<projected<<" driving="<<driving<<" locked="<<locked<<" dimension="<<dimension.id<<" factor="<<factor<<'\n';
            check(accepted,"Dimension edit contract failed");if(!driving)check(s.points==base.points,"Reference dimension drove geometry");equations(s);check(s.external_references==references,"Dimension edit changed external source");++edits;
        }
        // Every point, both free directions where applicable, and the coupled diagonal.
        for(auto action:{std::array{1.,0.,-1.},std::array{2.,1.,0.},std::array{2.,0.,-1.},std::array{3.,1.,1.},std::array{4.,-1.,0.},std::array{5.,-1.,0.},std::array{5.,0.,1.}}){
            s=base;const auto index=static_cast<std::size_t>(action[0]);
            for(double scale:{.2,.4,-.1}){const auto p=s.points[index];const auto before=s.serialized();const bool accepted=s.move_point(p.id,p.x+sx*scale*action[1],p.y+sy*scale*action[2]);check(accepted==!locked,"Allowed/locked point drag contract failed");if(accepted)check(std::hypot(s.find_point(p.id)->x-p.x,s.find_point(p.id)->y-p.y)>.01,"Free point did not move");else check(s.serialized()==before,"Locked drag changed document");equations(s);check(s.external_references==references,"Drag changed external source");s=Sketch::from_serialized(s.serialized());equations(s);++drags;}
        }
        s=base;reject_move(s,0,sx,sy);reject_move(s,1,sx,0);reject_move(s,4,0,sy);
        if(driving){s=base;s.dimensions[0].lower_limit=1;s.dimensions[0].upper_limit=30;const auto before=s.serialized();check(!s.set_dimension_value(s.dimensions[0].id,100),"Out-of-range dimension accepted");check(!s.set_dimension_value(s.dimensions[0].id,std::numeric_limits<double>::infinity()),"Nonfinite dimension accepted");check(before==s.serialized(),"Rejected dimension changed document");equations(s);}
    }
    auto combined=source;
    for(const auto& dimension:source.dimensions)for(double factor:{1.1,.95,1.05})
        check(combined.set_dimension_value(dimension.id,dimension_display_value(dimension,combined.plane)*factor),"Combined dimension sequence failed");
    for(double delta:{.8,-.5,.6}) {
        const auto p=combined.points[3];
        check(combined.move_point(p.id,p.x+delta,p.y+delta-2e-5),"Point drag near the coupled free path failed");equations(combined);
        check(std::hypot(combined.find_point(p.id)->x-p.x,combined.find_point(p.id)->y-p.y)>.03,"Projected coupled drag did not move");
    }
    // Production Tažení 009 topology: three equal-width rectangles are anchored
    // to immutable external lines and positioned by four driving dimensions.
    // Every driver must remain editable after the profile becomes fully constrained.
    for(bool mirrored:{false,true})for(bool reverse_support:{false,true})for(bool reverse_equal:{false,true})for(bool locate_from_midpoint:{false,true}){
        auto exact=three_rectangle_external_fixture(mirrored,reverse_support,reverse_equal,locate_from_midpoint);
        const auto references=exact.external_references;
        check(exact.solve().remaining_degrees_of_freedom==0,"Three-rectangle fixture is not fully constrained");
        for(std::size_t index=0;index<exact.dimensions.size();++index)for(double delta:{1.,-2.,.5}){
            const auto id=exact.dimensions[index].id;
            const auto current=dimension_display_value(exact.dimensions[index],exact.plane);
            check(exact.set_dimension_value(id,current+delta),"Fully constrained external-reference dimension edit failed");
            equations(exact);check(exact.external_references==references,"Dimension edit changed immutable external supports");
            exact=Sketch::from_serialized(exact.serialized());equations(exact);
        }
        const auto point=exact.points.front();
        check(exact.move_point(point.id,point.x+1,point.y+1),"Unlocked dimension drag was rejected");
        check(std::hypot(exact.find_point(point.id)->x-point.x,exact.find_point(point.id)->y-point.y)>.01,
            "Unlocked dimension drag did not move its point");
        equations(exact);check(exact.external_references==references,"Drag changed immutable external supports");
        for(auto& dimension:exact.dimensions)dimension.locked=true;
        const auto before=exact.serialized();const auto locked=exact.points.front();
        check(!exact.move_point(locked.id,locked.x+1,locked.y+1),"Locked fully constrained external profile escaped on drag");
        check(exact.serialized()==before,"Rejected locked drag changed the Sketch");
    }
    // Native Part persistence and real transaction history use the same edited Sketch.
    zima::document::DocumentSession session(part);auto changed=part;check(changed.sketches.back().set_dimension_value(source.dimensions.back().id,7),"Part edit failed");session.commit(changed);check(session.undo(),"Undo unavailable");check(session.document().sketches.back().serialized()==source.serialized(),"Undo did not restore Sketch");check(session.redo(),"Redo unavailable");equations(session.document().sketches.back());
    const auto output=std::filesystem::path("build/form-diagnostic/equal-length-reopen.prtz");std::filesystem::create_directories(output.parent_path());session.document().save(output);const auto reopened=zima::document::PartDocument::load(output);equations(reopened.sketches.back());check(reopened.sketches.back().serialized()==session.document().sketches.back().serialized(),"Native reopen changed edited Sketch");
    std::cout<<edits<<" dimension edits and "<<drags<<" point drags passed: both selection orders, four orientations, Distance/X/Y, driving/reference, locked/unlocked, native/external axes, independent equations, atomic rejection, serialization, native reopen and Undo/Redo\n";return 0;
}catch(const std::exception& e){std::cerr<<"Equal-length component: "<<e.what()<<'\n';return 1;}}
