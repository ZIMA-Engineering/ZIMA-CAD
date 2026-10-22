#include <zima/sketcher/sketch.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace zima::sketcher;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
Sketch source(const std::filesystem::path& path) {
    if(!path.empty()) {
        std::ifstream input(path);check(bool(input),"Cannot read rolled L source");
        std::string line;bool sketch=false;
        while(std::getline(input,line)) {
            if(line.starts_with('['))sketch=line.starts_with("[Sketch.");
            if(sketch&&line.starts_with("data="))return Sketch::from_serialized(line.substr(5));
        }
        throw std::runtime_error("Rolled L source has no native Sketch");
    }
    auto s=Sketch::create_default();const double size=44.91419982910156,t=8.409066197816243;
    for(auto p:std::array<std::array<double,2>,6>{{{0,0},{size,0},{0,size},{t,size},{size,t},{t,t}}})
        s.points.push_back(Sketch::create_point(p[0],p[1]));
    for(auto pair:std::array<std::array<unsigned,2>,6>{{{0,1},{0,2},{2,3},{1,4},{4,5},{5,3}}})
        s.segments.push_back(Sketch::create_segment(s.points[pair[0]].id,s.points[pair[1]].id));
    static_cast<void>(s.add_point_reference_constraint(s.points[0].id,"sketch_origin"));
    static_cast<void>(s.add_point_on_line_constraint(s.points[1].id,"sketch_axis:x"));
    static_cast<void>(s.add_point_on_line_constraint(s.points[2].id,"sketch_axis:y"));
    static_cast<void>(s.add_segment_pair_constraint(s.segments[0].id,s.segments[1].id,ConstraintKind::EqualLength));
    static_cast<void>(s.add_point_pair_constraint(s.points[2].id,s.points[3].id,ConstraintKind::Horizontal));
    static_cast<void>(s.add_point_pair_constraint(s.points[1].id,s.points[4].id,ConstraintKind::Vertical));
    static_cast<void>(s.add_segment_pair_constraint(s.segments[2].id,s.segments[3].id,ConstraintKind::EqualLength));
    static_cast<void>(s.add_point_pair_constraint(s.points[3].id,s.points[5].id,ConstraintKind::Vertical));
    static_cast<void>(s.add_point_pair_constraint(s.points[5].id,s.points[4].id,ConstraintKind::Horizontal));
    static_cast<void>(s.add_corner_fillet(s.segments[4].id,s.segments[3].id,4.428077697753905));
    static_cast<void>(s.add_corner_fillet(s.segments[5].id,s.segments[2].id,4.697865483704914));
    static_cast<void>(s.add_corner_fillet(s.segments[5].id,s.segments[4].id,11.709309580373713));
    return s;
}
void unfit_thickness(Sketch s) {
    const auto inner=std::ranges::max_element(s.corner_radii,{},&SketchCornerRadius::radius);
    const auto origin=std::ranges::find_if(s.constraints,[](const auto& c){return c.kind==ConstraintKind::PointReference&&c.second_point_id=="sketch_origin";});
    check(inner!=s.corner_radii.end()&&origin!=s.constraints.end(),"Rolled L source lacks its origin or inside radius");
    const auto dimension=s.create_point_dimension(origin->first_point_id,inner->vertex_id,DimensionKind::DistanceX);
    s.apply_dimension(dimension);static_cast<void>(s.viewer_mesh());
    const auto before=s.serialized();
    const bool accepted=s.set_dimension_value(dimension.id,2.);
    std::cout<<"Unfit thickness accepted="<<accepted<<" source_changed="<<(s.serialized()!=before)<<std::endl;
    if(accepted)try{static_cast<void>(s.viewer_mesh());std::cout<<"Unexpectedly renderable unfit profile\n";}
        catch(const std::exception& error){std::cout<<"Subsequent profile publication throws: "<<error.what()<<std::endl;}
    check(!accepted&&s.serialized()==before,"Unfit rolled L thickness was committed before profile validation");
}
void verify(const Sketch& s,bool equal) {
    const auto near=[](double a,double b){return std::abs(a-b)<2e-6;};
    check(near(s.points[0].x,0)&&near(s.points[0].y,0),"Origin contact escaped");
    check(near(s.points[1].y,0)&&near(s.points[2].x,0),"Axis contact escaped");
    check(near(std::abs(s.points[1].x),std::abs(s.points[2].y)),"Equal outer legs escaped");
    check(near(s.points[2].y,s.points[3].y)&&near(s.points[1].x,s.points[4].x),"Outer H/V escaped");
    check(near(s.points[3].x,s.points[5].x)&&near(s.points[5].y,s.points[4].y),"Inner H/V escaped");
    check(near(std::abs(s.points[3].x),std::abs(s.points[4].y)),"Equal thickness escaped");
    if(equal)check(near(s.corner_radii[0].radius,s.corner_radii[1].radius),"Equal toe radii escaped");
    const auto before=s.serialized();const auto p=s.evaluated_profile_sketch();static_cast<void>(s.viewer_mesh());
    check(s.serialized()==before,"Profile publication changed source geometry");
    check(p.arcs.size()==3,"Rolled L lost one of its three exact arcs");
    for(const auto& arc:p.arcs) {
        const auto* c=p.find_point(arc.center_point_id);check(c,"Arc center missing");
        for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
            const auto* q=p.find_point(id);check(q,"Arc endpoint missing");
            check(near(std::hypot(q->x-c->x,q->y-c->y),arc.radius),"Arc endpoint violates circle equation");
            bool tangent=false;
            for(const auto& line:p.segments)if(line.first_point_id==id||line.second_point_id==id) {
                const auto* other=p.find_point(line.first_point_id==id?line.second_point_id:line.first_point_id);
                const double length=std::hypot(other->x-q->x,other->y-q->y);
                tangent=length>1e-8&&std::abs((q->x-c->x)*(other->x-q->x)+(q->y-c->y)*(other->y-q->y))/length<2e-6;
                if(tangent)break;
            }
            check(tangent,"Rounded corner lost tangent incidence");
        }
    }
    for(const auto& d:s.dimensions)if(d.geometry_id.empty()&&!d.second_point_id.empty()) {
        const auto* a=s.find_point(d.first_point_id);const auto* b=s.find_point(d.second_point_id);check(a&&b,"Point dimension identity missing");
        const double actual=d.kind==DimensionKind::DistanceX?std::abs(b->x-a->x):d.kind==DimensionKind::DistanceY?std::abs(b->y-a->y):std::hypot(b->x-a->x,b->y-a->y);
        check(near(std::abs(d.value),actual),"Point dimension disagrees with independent equation");
    }
}
void point_matrix(const Sketch& original) {
    unsigned cases=0,reference_locks=0,rejected_zero=0,failures=0;
    for(double sx:{1.,-1.})for(double sy:{1.,-1.})
    for(const auto pair:std::array<std::array<unsigned,2>,3>{{{0,5},{1,4},{2,3}}})
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceX,DimensionKind::DistanceY})
    for(bool reverse:{false,true})for(bool equal_first:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        std::cout<<"Point case "<<cases<<" signs "<<sx<<','<<sy<<" pair "<<pair[0]<<','<<pair[1]<<" kind "<<int(kind)<<" reverse "<<reverse<<" equal-first "<<equal_first<<" driving "<<driving<<" locked "<<locked<<std::endl;
        try {
        auto s=original;for(auto& p:s.points){p.x*=sx;p.y*=sy;}
        const auto equal=[&]{static_cast<void>(s.add_equal_radius_constraint(s.corner_radii[0].id,s.corner_radii[1].id));};
        if(equal_first)equal();
        auto d=s.create_point_dimension(s.points[pair[reverse?1:0]].id,s.points[pair[reverse?0:1]].id,kind);
        d.driving=driving;d.locked=locked;const double initial=std::abs(d.value);const auto before=s.serialized();
        bool accepted=true;try{s.apply_dimension(d);}catch(const std::exception&){accepted=false;}
        if(locked&&!driving) {
            check(!accepted&&s.serialized()==before,"Reference lock was not rejected atomically");++reference_locks;++cases;continue;
        }
        if(!accepted) {
            check(driving&&initial<1e-7&&s.serialized()==before,"A usable rolled L point dimension was rejected or mutated its source");
            ++rejected_zero;++cases;continue;
        }
        if(!equal_first)equal();verify(s,true);
        for(double scale:{1.05,1.1,1.025}) {
            const auto snapshot=s.serialized();
            if(driving&&initial>1e-7) {
                if(!s.set_dimension_value(d.id,initial*scale)) {
                    auto changed=*std::ranges::find(s.dimensions,d.id,&SketchDimension::id);changed.value=initial*scale;
                    try{auto diagnostic=s;diagnostic.apply_dimension(changed);}catch(const std::exception& error){std::cerr<<"Point edit reason: "<<error.what()<<'\n';}
                    throw std::runtime_error("Valid point dimension successive edit rejected");
                }
                check(s.serialized()!=snapshot,"Driving dimension edit did not move geometry");
            }else if(driving) {
                check(!s.set_dimension_value(d.id,1.)&&s.serialized()==snapshot,"Impossible zero-projection edit changed the document");
            }
            verify(s,true);
            const auto vertex=s.points[5].id;const auto p=*s.find_point(vertex);
            const auto frozen=s.serialized();const bool moved=s.move_point(vertex,p.x+sx*.2,p.y+sy*.2);
            if(driving&&locked&&initial>1e-7)check(!moved&&s.serialized()==frozen,"Locked thickness escaped on drag");
            else {check(moved,"Permitted thickness drag was rejected");const auto* after=s.find_point(vertex);check(std::hypot(after->x-p.x,after->y-p.y)>.1,"Permitted thickness drag did not move");}
            verify(s,true);const auto saved=s.serialized();s=Sketch::from_serialized(saved);
            check(s.serialized()==saved,"Rolled L point dimension identity changed on reopen");verify(s,true);
        }
        ++cases;
        }catch(const std::exception& error){++cases;++failures;std::cerr<<"CASE FAILED: "<<error.what()<<std::endl;}
    }
    std::cout<<"Point matrix: "<<cases<<" cases, "<<reference_locks<<" reference-lock and "<<rejected_zero<<" zero-projection atomic rejections, "<<failures<<" failures\n";
    check(failures==0,"Rolled L point matrix has unresolved edit/drag failures");
}
void radius_matrix(const Sketch& original) {
    unsigned cases=0,rejected=0,reference_order_rejections=0;
    for(unsigned corner=0;corner<3;++corner)for(bool diameter:{false,true})
    for(bool equal_first:{false,true})for(bool reverse_equal:{false,true})
    for(bool driving:{false,true})for(bool locked:{false,true}) {
        std::cout<<"Radius case "<<cases<<" corner "<<corner<<" D "<<diameter<<" equal-first "<<equal_first<<" equal-reverse "<<reverse_equal<<" driving "<<driving<<" locked "<<locked<<std::endl;
        auto s=original;const auto equal=[&]{static_cast<void>(s.add_equal_radius_constraint(s.corner_radii[reverse_equal?1:0].id,s.corner_radii[reverse_equal?0:1].id));};
        if(equal_first)equal();
        auto d=diameter?s.create_arc_diameter_dimension(s.corner_radii[corner].id):s.create_arc_radius_dimension(s.corner_radii[corner].id);
        d.driving=driving;d.locked=locked;
        if(locked&&!driving) {
            const auto before=s.serialized();bool failed=false;try{s.apply_dimension(d);}catch(const std::exception&){failed=true;}
            check(failed&&s.serialized()==before,"Radius reference lock was not rejected atomically");++rejected;++cases;continue;
        }
        s.apply_dimension(d);
        if(!equal_first) {
            const auto before=s.serialized();bool accepted=true;try{equal();}catch(const std::exception&){accepted=false;}
            if(!accepted) {
                check(driving&&corner<2&&corner!=unsigned(reverse_equal?1:0)&&s.serialized()==before,"Equal-radius insertion unexpectedly failed or changed its source");
                ++reference_order_rejections;++cases;continue;
            }
        }
        verify(s,true);
        for(double scale:{.95,.9,1.025}) {
            const double value=(corner==2?11.:4.)*scale;
            const auto points=s.points;
            if(driving)check(s.set_dimension_value(d.id,diameter?2*value:value),"Successive valid radius edit rejected");
            else static_cast<void>(s.add_corner_fillet(s.corner_radii[corner].first_segment_id,s.corner_radii[corner].second_segment_id,value));
            check(s.points==points,"Corner parameter edit moved authored design points");verify(s,true);
            const auto frozen=s.serialized();if(driving)check(!s.set_dimension_value(d.id,100)&&s.serialized()==frozen,"Impossible radius edit changed the document");
            const double length=45+scale;check(s.move_point(s.points[1].id,length,0),"Permitted leg drag with radius dimension rejected");
            check(std::abs(s.points[1].x-length)<1e-6,"Permitted leg drag did not reach target");verify(s,true);
            const auto saved=s.serialized();s=Sketch::from_serialized(saved);check(s.serialized()==saved,"Radius identity changed on reopen");verify(s,true);
        }
        for(auto& point:s.points)point.fixed=true;
        const auto frozen=s.serialized();check(!s.move_point(s.points[5].id,12,12)&&s.serialized()==frozen,"Fully fixed rounded L escaped its constraints");verify(s,true);
        ++cases;
    }
    std::cout<<"Radius matrix: "<<cases<<" cases, "<<rejected<<" atomic reference-lock and "<<reference_order_rejections<<" reference-order rejections\n";
}
void reported_sequence(const Sketch& original) {
    for(double size:{original.points[2].y,30.,40.,50.})for(bool point_line:{false,true}) {
        auto s=original;
        std::cout<<"Reported sequence left length="<<size<<" point-line="<<point_line<<std::endl;
        auto length=s.create_segment_dimension(s.segments[1].id);s.apply_dimension(length);
        check(s.set_dimension_value(length.id,size),"Long left leg length edit rejected");verify(s,false);
        auto d=point_line?s.create_point_line_dimension(s.points[1].id,s.segments[4].id):s.create_point_dimension(s.points[1].id,s.points[4].id,DimensionKind::DistanceY);
        auto preview=s;preview.dimensions.push_back(d);static_cast<void>(preview.viewer_mesh());
        s.apply_dimension(d);verify(s,false);
        std::cout<<"Reported sequence preview and placement completed, dimensions="<<s.dimensions.size()<<std::endl;
    }
}
}
int main(int argc,char** argv) {try {
    auto s=source(argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path{});
    check(s.segments.size()==6&&s.corner_radii.size()==3,"Rolled L fixture needs six legs and three corners");
    reported_sequence(s);
    if(argc>2&&std::string_view(argv[2])=="--sequence")return 0;
    unfit_thickness(s);
    radius_matrix(s);
    point_matrix(s);
    std::cout<<"Rolled L profile checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
