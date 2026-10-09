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
        else throw std::runtime_error("Unchecked fixture constraint");
    }
    for(const auto& d:s.dimensions)if(!d.suppressed){const auto a=s.find_point(d.first_point_id),b=s.find_point(d.second_point_id);const auto dx=b->x-a->x,dy=b->y-a->y;near(d.value,d.kind==DimensionKind::Distance?std::hypot(dx,dy):d.kind==DimensionKind::DistanceX?dx:dy);}
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
}
int main(){try{
    auto part=zima::document::PartDocument::load("cpp/tests/fixtures/sketch/form-edge-equal-length.prtz");const auto source=part.sketches.back();equations(source);std::size_t edits{},drags{};
    for(int sx:{-1,1})for(int sy:{-1,1})for(bool reverse:{false,true})for(bool external:{false,true})for(bool projected:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}){
        if(!driving&&locked)continue; // Locked reference dimensions are invalid by contract.
        auto base=variant(source,sx,sy,reverse,external,projected,driving,locked);const auto references=base.external_references;
        auto s=base;
        for(const auto& dimension:base.dimensions)for(double factor:{.8,1.2,1.05}){
            const auto before=s.serialized();const bool accepted=s.set_dimension_value(dimension.id,dimension_display_value(dimension)*factor);
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
        check(combined.set_dimension_value(dimension.id,dimension_display_value(dimension)*factor),"Combined dimension sequence failed");
    for(double delta:{.8,-.5,.6}) {
        const auto p=combined.points[3];
        check(combined.move_point(p.id,p.x+delta,p.y+delta-2e-5),"Point drag near the coupled free path failed");equations(combined);
        check(std::hypot(combined.find_point(p.id)->x-p.x,combined.find_point(p.id)->y-p.y)>.03,"Projected coupled drag did not move");
    }
    // Native Part persistence and real transaction history use the same edited Sketch.
    zima::document::DocumentSession session(part);auto changed=part;check(changed.sketches.back().set_dimension_value(source.dimensions.back().id,7),"Part edit failed");session.commit(changed);check(session.undo(),"Undo unavailable");check(session.document().sketches.back().serialized()==source.serialized(),"Undo did not restore Sketch");check(session.redo(),"Redo unavailable");equations(session.document().sketches.back());
    const auto output=std::filesystem::path("build/form-diagnostic/equal-length-reopen.prtz");std::filesystem::create_directories(output.parent_path());session.document().save(output);const auto reopened=zima::document::PartDocument::load(output);equations(reopened.sketches.back());check(reopened.sketches.back().serialized()==session.document().sketches.back().serialized(),"Native reopen changed edited Sketch");
    std::cout<<edits<<" dimension edits and "<<drags<<" point drags passed: both selection orders, four orientations, Distance/X/Y, driving/reference, locked/unlocked, native/external axes, independent equations, atomic rejection, serialization, native reopen and Undo/Redo\n";return 0;
}catch(const std::exception& e){std::cerr<<"Equal-length component: "<<e.what()<<'\n';return 1;}}
