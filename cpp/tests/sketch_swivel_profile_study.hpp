#pragma once
#include "sketch_coupled_dimension_study.hpp"
#include "sketch_mechanism_study.hpp"
namespace zima::test::engineering {
using namespace mechanism;
struct Swivel {
    Sketch sketch=Sketch::create_default();
    std::vector<std::string> drivers;
    std::string right_center,left_top,right_top,corner_start,corner_center;
};
// Reconstruct MIT ESP Training p.108 at scale 10. The R15 transition is
// tangent to the R20-side horizontal, with the intentional corner at x=60.
inline Swivel swivel(double rotation,bool reverse) {
    Swivel f;auto& s=f.sketch;
    const auto datum=s.add_segment(-100,0,150,0,1e-7,true);
    for(const auto& id:{s.segments[0].first_point_id,s.segments[0].second_point_id})s.find_point(id)->fixed=true;
    std::vector<std::string> holes;for(const auto p:{XY{0,10},{0,15},{48,5},{96,10}})holes.push_back(s.add_circle(p[0],0,p[1]));
    const auto origin=s.circles[0].center_point_id,middle=s.circles[2].center_point_id;
    f.right_center=s.circles[3].center_point_id;s.find_point(origin)->fixed=true;
    static_cast<void>(s.add_point_on_line_constraint(middle,datum));static_cast<void>(s.add_point_on_line_constraint(f.right_center,datum));
    const auto left=s.add_arc(0,0,0,30,0,-30),right=s.add_arc(96,0,96,-20,96,20);
    f.left_top=s.arcs[0].start_point_id;f.right_top=s.arcs[1].end_point_id;
    const double x=60+std::sqrt(200.);
    const auto upper=s.add_segment(0,30,60,30),corner=s.add_arc(x,35,60,30,x,20),inner=s.add_segment(x,20,96,20);
    f.corner_center=s.arcs[2].center_point_id;f.corner_start=s.arcs[2].start_point_id;
    for(const auto& line:{upper,inner}){auto angle=s.create_segment_dimension(line,DimensionKind::Angle);angle.locked=true;s.apply_dimension(angle);}
    for(const auto& pair:{std::pair{left,upper},std::pair{right,inner},std::pair{corner,inner}})
        static_cast<void>(s.add_tangent_constraint(reverse?pair.second:pair.first,reverse?pair.first:pair.second));
    const auto copy=s.mirror_geometry(reverse?std::vector{inner,corner,upper}:std::vector{upper,corner,inner},datum);
    // These two joins are deliberate incidences in the source drawing. C is
    // the native merge operation, and must rewire the reflected relations.
    for(const auto& id:{s.arcs[0].end_point_id,s.arcs[1].start_point_id}) {
        const auto p=position(s,id);std::string reflected;
        for(const auto& candidate:copy.point_ids)if(s.find_point(candidate)) {const auto q=position(s,candidate);if(std::hypot(q[0]-p[0],q[1]-p[1])<1e-7){reflected=candidate;break;}}
        require(!reflected.empty(),"Swivel mirror has no matching junction");static_cast<void>(s.merge_points(id,reflected));
    }
    for(const auto& hole:holes){auto dim=s.create_circle_diameter_dimension(hole);dim.locked=true;s.apply_dimension(dim);f.drivers.push_back(dim.id);}
    for(const auto& arc:{left,right,corner}){auto dim=s.create_arc_radius_dimension(arc);dim.locked=true;s.apply_dimension(dim);f.drivers.push_back(dim.id);}
    for(const auto& point:{middle,f.right_center}) {auto dim=s.create_point_dimension(reverse?point:origin,reverse?origin:point);dim.locked=true;s.apply_dimension(dim);f.drivers.push_back(dim.id);}
    auto length=s.create_segment_dimension(upper);length.locked=true;s.apply_dimension(length);f.drivers.push_back(length.id);
    const double c=std::cos(rotation*radians),d=std::sin(rotation*radians);
    for(auto& p:s.points){const double x=p.x;p.x=c*x-d*p.y;p.y=d*x+c*p.y;}
    for(auto& arc:s.arcs){arc.start_angle+=rotation*radians;arc.end_angle+=rotation*radians;}
    for(auto& dim:s.dimensions)if(dim.kind==DimensionKind::Angle)dim.value+=rotation;
    s.validate();return f;
}
inline void check_swivel(const Swivel& f) {
    const auto& s=f.sketch;check_coupled_equations(s);
    for(std::size_t i=0;i<s.points.size();++i)for(std::size_t j=i+1;j<s.points.size();++j)
        require(std::hypot(s.points[i].x-s.points[j].x,s.points[i].y-s.points[j].y)>1e-7,"Swivel has duplicate native junctions");
    require(s.circles.size()==4&&s.arcs.size()==4&&s.segments.size()==5,"Swivel profile changed its native topology");
}
inline int run_swivel() {
    unsigned passed{},failed{},edits{},drags{};
    for(double rotation:{0.,90.,27.})for(bool reverse:{false,true})for(unsigned active=0;active<10;++active)for(int state=0;state<3;++state) {
        try {
            auto f=swivel(rotation,reverse);auto& s=f.sketch;check_swivel(f);
            auto dim=*std::ranges::find(s.dimensions,f.drivers[active],&SketchDimension::id);
            dim.driving=state!=0;dim.locked=state==2;s.apply_dimension(dim);check_swivel(f);
            auto status=s.solve();require(status.remaining_degrees_of_freedom==(state==0?1:0),"Unexpected swivel DOF="+std::to_string(status.remaining_degrees_of_freedom));
            if(state)for(double value:{dim.value+1,dim.value-1,dim.value}) {
                require(s.set_dimension_value(dim.id,value),"Swivel numeric edit rejected");check_swivel(f);++edits;
            }
            if(active>=4&&active!=7) {
                const auto handle=active==4?f.left_top:active==5?f.right_top:active==6?f.corner_center:active==8?f.right_center:f.corner_start;
                const double c=std::cos(rotation*radians),d=std::sin(rotation*radians);
                for(double delta:{.5,-.7,.3}) {
                    const auto p=position(s,handle);const auto before=s.serialized();
                    const bool horizontal=active>=8;const XY offset=horizontal?XY{c*delta-d*.1,d*delta+c*.1}:XY{c*.1-d*delta,d*.1+c*delta};
                    const bool moved=s.move_point(handle,p[0]+offset[0],p[1]+offset[1]);
                    if(state==2)require(!moved&&s.serialized()==before,"Locked swivel escaped during drag");
                    else {
                        require(moved,"Editable swivel junction cannot drag");const auto q=position(s,handle);
                        require(std::hypot(q[0]-p[0],q[1]-p[1])>1e-4,"Swivel drag reported no movement");check_swivel(f);++drags;}
                }
            }
            auto authored=part(s);const auto file=std::filesystem::temp_directory_path()/("zima-swivel-"+authored.document_id+".prtz");
            authored.save(file);const auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
            require(loaded.sketches[0].serialized()==authored.sketches[0].serialized(),"Swivel native reopen changed its authored graph");
            ++passed;
        } catch(const std::exception& e){++failed;std::cerr<<"FAIL swivel rotation="<<rotation<<" reverse="<<reverse<<" dimension="<<active<<" state="<<state<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Swivel profile study: "<<passed<<" passed, "<<failed<<" failed; "<<edits<<" numeric edits, "<<drags<<" moving drags\n";
    return failed?1:0;
}
}
