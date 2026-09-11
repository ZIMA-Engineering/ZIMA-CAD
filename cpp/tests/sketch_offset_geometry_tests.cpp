#include <zima/sketcher/curve_geometry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
double error(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
int main(){try {
    auto s=sketcher::Sketch::create_default();
    const auto line=s.add_segment(0,0,20,0);
    auto c=sketcher::sketch_curve_geometry(s,line);
    auto o=sketcher::offset_curve_geometry(c,2);
    check(error(kernel::bspline_value(o,.3),{6,2,0})<1e-10,"Line offset");
    const auto circle=s.add_circle(0,0,10);
    c=sketcher::sketch_curve_geometry(s,circle);
    for(int i=0;i<=1000;++i)check(std::abs(std::hypot(kernel::bspline_value(c,i/1000.).x,kernel::bspline_value(c,i/1000.).y)-10)<1e-10,"Exact circle conversion");
    o=sketcher::offset_curve_geometry(c,2);
    for(int i=0;i<=4096;++i)check(error(kernel::bspline_value(o,i/4096.),sketcher::curve_offset_point(c,i/4096.,2))<1e-5,"Circle offset error");
    c={3,{{0,0,0},{2,7,0},{9,-3,0},{14,5,0},{20,0,0}},{0,0,0,0,.35,1,1,1,1},{1,.7,1.8,1,1}};
    auto t=sketcher::trim_curve_geometry(c,.13,.86);
    for(int i=0;i<=4096;++i)check(error(kernel::bspline_value(t,i/4096.),kernel::bspline_value(c,.13+.73*i/4096.))<1e-10,"Rational trim preserves supporting shape");
    for(double d:{-.1,.1,.3}){
        o=sketcher::offset_curve_geometry(c,d);
        for(int i=0;i<=4096;++i)check(error(kernel::bspline_value(o,i/4096.),sketcher::curve_offset_point(c,i/4096.,d))<1e-5,"Spline offset error");
    }
    auto linked=sketcher::Sketch::create_default();
    const auto source=linked.add_segment(0,0,20,0);
    const auto dependent=linked.add_offset(source,2,false);
    auto restored=sketcher::Sketch::from_serialized(linked.serialized());
    check(restored.find_offset(dependent)!=nullptr,"Persisted dependency");
    const auto source_pieces=restored.retain_curve_intervals(source,{{.2,.8}});
    check(source_pieces.front()==source,"Trim retains source identity");
    check(error(kernel::bspline_value(restored.supporting_curve(source),0),{0,0,0})<1e-10,"Trim retains whole support");
    check(error(kernel::bspline_value(sketcher::sketch_curve_geometry(restored,dependent),0),{0,2,0})<1e-10,"Source trim does not destroy offset");
    const auto pieces=restored.retain_curve_intervals(dependent,{{.1,.9}});
    check(pieces.front()==dependent,"Trim retains offset identity");
    restored.update_offset(dependent,3,true);
    check(error(kernel::bspline_value(sketcher::sketch_curve_geometry(restored,dependent),0),{2,-3,0})<1e-10,"Trimmed offset remains editable");
    restored=sketcher::Sketch::from_serialized(restored.serialized());
    const auto before=sketcher::sketch_curve_geometry(restored,dependent);
    restored.free_offset(dependent);
    check(!restored.find_offset(dependent)&&sketcher::sketch_curve_geometry(restored,dependent)==before,"Free keeps geometry");
    auto moving=sketcher::Sketch::create_default();
    const auto root=moving.add_segment(0,0,20,0);
    const auto follower=moving.add_offset(root,2,false);
    moving.find_point(moving.segments.front().second_point_id)->y=5;
    moving.refresh_curve_dependencies();
    check(error(kernel::bspline_value(sketcher::sketch_curve_geometry(moving,follower),.5),sketcher::curve_offset_point(moving.supporting_curve(root),.5,2))<1e-10,"Follow source change");
    check(!moving.move_point(moving.bsplines.front().control_point_ids.front(),100,100),"Dependent handles cannot move");
    auto intersected=sketcher::Sketch::create_default();
    const auto baseline=intersected.add_segment(0,0,20,0);
    const auto shifted=intersected.add_offset(baseline,2,false);
    const auto cutter=intersected.add_segment(10,-5,10,5);
    static_cast<void>(intersected.retain_curve_intervals(shifted,{{0,.5}}));
    check(intersected.find_offset(shifted)->end_anchor.has_value(),"Trim saves cutter identity");
    for(auto& segment:intersected.segments)if(segment.id==cutter){intersected.find_point(segment.first_point_id)->x=10.2;intersected.find_point(segment.second_point_id)->x=10.2;}
    intersected.refresh_curve_dependencies();
    check(!intersected.find_offset(shifted)->broken,"Small cutter change keeps branch");
    check(error(kernel::bspline_value(sketcher::sketch_curve_geometry(intersected,shifted),1),{10.2,2,0})<1e-7,"Trim endpoint follows intersection");
    intersected=sketcher::Sketch::from_serialized(intersected.serialized());
    intersected.remove_geometry(cutter);intersected.refresh_curve_dependencies();
    check(intersected.find_offset(shifted)->broken,"Missing cutter is not silently replaced");
    auto periodic=sketcher::Sketch::create_default();
    const auto periodic_id=periodic.add_bspline({{0,0},{5,0},{5,5},{0,5}},3,true);
    auto periodic_curve=sketcher::sketch_curve_geometry(periodic,periodic_id);
    check(error(kernel::bspline_value(periodic_curve,0),kernel::bspline_value(periodic_curve,1))<1e-10,"Periodic support closes");
    const auto mesh=periodic.viewer_mesh();
    for(const auto& edge:mesh.edges)if(edge.reference.semantic_key=="bspline:"+periodic_id)
        for(std::size_t i=0;i<edge.points.size();++i)check(error(kernel::bspline_value(periodic_curve,double(i)/(edge.points.size()-1)),edge.points[i])<1e-9,"Periodic conversion retains shape");
    bool collapse=false;try{static_cast<void>(sketcher::offset_curve_geometry(sketcher::sketch_curve_geometry(s,circle),11));}catch(const std::exception&){collapse=true;}
    check(collapse,"Collapsed circular offset rejected");
    auto branched=sketcher::Sketch::create_default();const auto closed_source=branched.add_circle(0,0,10);
    const auto closed_offset=branched.add_offset(closed_source,1,false);
    const auto halves=branched.retain_curve_intervals(closed_offset,{{.75,1},{0,.25}});
    branched.update_offset(halves.back(),2,false);
    check(branched.find_offset(halves.front())->distance==2,"Offset pieces must share one parameter edit");
    const auto a=sketcher::sketch_curve_geometry(branched,halves.front()),b=sketcher::sketch_curve_geometry(branched,halves.back());
    check(error(kernel::bspline_value(a,1),kernel::bspline_value(b,0))<1e-10,"Closed support seam lost its common point");
    auto chain=sketcher::Sketch::create_default();const auto root_curve=chain.add_segment(0,0,20,0);
    const auto parent_offset=chain.add_offset(root_curve,1,false);static_cast<void>(chain.retain_curve_intervals(parent_offset,{{.2,.8}}));
    const auto child_offset=chain.add_offset(parent_offset,1,false);
    const auto child_before=sketcher::sketch_curve_geometry(chain,child_offset);
    chain.free_offset(parent_offset);
    const auto child_after=sketcher::sketch_curve_geometry(chain,child_offset);
    for(int i=0;i<=128;++i)check(error(kernel::bspline_value(child_before,i/128.),kernel::bspline_value(child_after,i/128.))<1e-10,"Freeing a trimmed source must not trim its child again");
    std::cout<<"Native offset and exact interval tests passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
