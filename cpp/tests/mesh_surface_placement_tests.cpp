#include <zima/document/part_document.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/placement_mesh_surface.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <numbers>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6){check(std::isfinite(a)&&std::abs(a-b)<tolerance,"Unexpected mesh contact coordinate");}
document::ConstructionReference surface(){return {{},"patch","face",0,true,"front",true};}
void plane(kernel::ViewerReferenceGeometry& g,const char* id,kernel::Vec3 normal) {
    auto s=std::make_shared<kernel::SurfaceGeometry>();s->axis=normal;
    s->radial=normal.x?kernel::Vec3{0,1,0}:kernel::Vec3{1,0,0};
    g.triangle_references.push_back({id,"face",{},s});g.triangles.insert(g.triangles.end(),{0,1,2});
}
kernel::ViewerReferenceGeometry bowl(int count=20) {
    kernel::ViewerReferenceGeometry g;
    for(int i=0;i<count;++i) {
        const double x=-10.+20.*i/count,next=-10.+20.*(i+1)/count;
        const auto base=static_cast<unsigned>(g.vertices.size());
        g.vertices.insert(g.vertices.end(),{{x,-5,x*x},{next,-5,next*next},{next,5,next*next},{x,5,x*x}});
        g.triangles.insert(g.triangles.end(),{base,base+1,base+2,base,base+2,base+3});
        g.triangle_references.insert(g.triangle_references.end(),2,{"patch","face",{}});
    }
    return g;
}
void constraints() {
    auto g=bowl();plane(g,"y",{0,1,0});plane(g,"z",{0,0,1});
    document::Placement p;p.x=2.4;p.y=1;p.z=6;p.references={surface()};
    check(document::resolve_placement(p,g),"Mesh first contact failed");near(p.x,2.4);near(p.y,1);near(p.z,6);
    check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==2,"Mesh first contact must leave two translations");
    p.references.push_back({{},"y","face",1,true});
    check(document::resolve_placement(p,g),"Mesh and plane failed");
    p.references.push_back({{},"z","face",6,true});
    check(document::resolve_placement(p,g),"Mesh and two planes failed");near(p.x,2.4);
    check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==0,"Fully defined mesh contact left a translation");
    const auto choices=document::placement_solution_branches(p,g);check(choices.size()==2,"Expected two distinct mesh solutions");
    near(choices[0].x,-2.4);near(choices[1].x,2.4);
    for(auto chosen:choices) {
        chosen=nlohmann::json(chosen).get<document::Placement>();
        auto stored=document::load_viewer_reference_geometry(document::serialize_viewer_reference_geometry(g));
        check(document::resolve_placement(chosen,stored),"Saved mesh solution failed");
        near(chosen.x,chosen.x<0?-2.4:2.4);
        // Reorder complete triangles: topology identity and selected root must survive.
        for(std::size_t i=0;i<20;++i)for(int c=0;c<3;++c)std::swap(stored.triangles[i*3+c],stored.triangles[(39-i)*3+c]);
        const auto x=chosen.x;check(document::resolve_placement(chosen,stored),"Reordered mesh failed");near(chosen.x,x);
    }
    const auto before=p;p.references.back().offset=-1;
    check(!document::resolve_placement(p,g)&&!p.reference_valid,"Impossible mesh equations accepted");near(p.x,before.x);near(p.z,before.z);
    p=before;p.references[1].offset=6;
    check(!document::resolve_placement(p,g),"Mesh extrapolated outside trimmed boundary");
    p=before;p.references.push_back({{},"y","face",2,true});
    check(!document::resolve_placement(p,g),"Contradictory reference accepted");
    p=before;p.references.front().instance_path="other-occurrence";
    check(!document::resolve_placement(p,g),"Reference resolved another occurrence's surface");
    auto changed=bowl();for(auto& vertex:changed.vertices)vertex.z+=2;
    plane(changed,"y",{0,1,0});plane(changed,"z",{0,0,1});p=before;
    check(document::resolve_placement(p,changed),"Changed source did not resolve");near(p.x,2);near(p.z,6);
    kernel::ViewerAxis axis;axis.point={2.4,1,100};axis.direction={0,0,1};axis.reference={"vertical","axis",{}};g.axes.push_back(axis);
    p=before;p.references={surface(),{{},"vertical","axis"}};
    check(document::resolve_placement(p,g),"General surface and straight axis failed");near(p.x,2.4);near(p.y,1);near(p.z,6);
    g.points.push_back({{2.4,1,6},{"anchor","point",{}}});
    p.references={surface(),{{},"anchor","point"}};
    check(document::resolve_placement(p,g),"General surface and point failed");near(p.x,2.4);near(p.z,6);
    // A datum plane remains unbounded and exact, independent of displayed triangles.
    p={};p.x=100;p.y=99;p.references={{{},"z","face",7,true}};
    check(document::resolve_placement(p,g),"Analytic plane regressed");near(p.x,100);near(p.y,99);near(p.z,7);
}
void transformed() {
    auto g=bowl();for(auto& v:g.vertices)v={20-v.y,v.x-10,v.z+7};
    for(auto& face:g.triangle_references)face.instance_path="nested/instance";
    plane(g,"x",{1,0,0});plane(g,"z",{0,0,1});
    auto ref=surface();ref.instance_path="nested/instance";
    document::Placement p;p.x=19;p.y=-7.6;p.z=13;
    p.references={ref,{{},"x","face",19,true},{{},"z","face",13,true}};
    check(document::resolve_placement(p,g),"Transformed occurrence failed");near(p.x,19);near(p.y,-7.6);near(p.z,13);
    check(document::placement_solution_branches(p,g).size()==2,"Transformed occurrence lost alternatives");
}
void sides() {
    for(bool reverse:{false,true})for(bool flip:{false,true})for(double offset:{0.,-0.,2.,-2.}) {
        auto g=bowl();if(reverse)for(std::size_t i=0;i<g.triangles.size();i+=3)std::swap(g.triangles[i+1],g.triangles[i+2]);
        auto ref=surface();ref.offset=offset;ref.flip=flip;
        const auto normal=document::placement_mesh::mul(kernel::Vec3{-5,0,1},(reverse?-1.:1.)/std::sqrt(26.));
        const auto anchor=document::placement_mesh::add({2.4,1,6},document::placement_mesh::mul(normal,offset));
        document::Placement p;p.x=anchor.x;p.y=anchor.y;p.z=anchor.z;p.references={ref};
        // Large offsets may have a closer patch; the exact seeded patch still has zero residual.
        check(document::resolve_placement(p,g),"Offset mesh contact failed");near(p.x,anchor.x);near(p.y,anchor.y);near(p.z,anchor.z);
        const double r=std::numbers::pi/180,cx=std::cos(p.rotation_x*r),sx=std::sin(p.rotation_x*r),
            cy=std::cos(p.rotation_y*r),sy=std::sin(p.rotation_y*r),cz=std::cos(p.rotation_z*r),sz=std::sin(p.rotation_z*r);
        const kernel::Vec3 front{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx};
        near(document::placement_mesh::dot(front,normal),flip?-1:1);
        const auto stored=nlohmann::json(p).get<document::Placement>();
        check(stored.references[0].flip==flip&&std::signbit(stored.references[0].offset)==std::signbit(offset),"Mesh side or signed zero lost");
    }
}
void benchmark(const kernel::ViewerReferenceGeometry& g,const document::ConstructionReference& ref) {
    document::Placement p;p.x=2;p.y=1;p.z=4;p.references={ref};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<30;++i)check(document::resolve_placement(p,g),"Repeated mesh placement failed");
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;
    std::cout<<"Mesh contact: "<<g.triangle_references.size()<<" reference triangles, mean "<<ms<<" ms\n";
    check(ms<500,"Mesh contact exceeded interactive time budget");
}
}
int main(){try {
    constraints();sides();transformed();benchmark(bowl(10000),surface());
    if(const char* path=std::getenv("ZIMA_VERIFY_MESH_SOURCE")) {
        std::vector<kernel::BodyResult> results;auto part=document::PartDocument::load(path,&results);
        check(!results.empty(),"Representative Part has no saved reference geometry");
        const auto& g=results.back().mesh.original_references;
        bool found=false;
        for(const auto& face:g.triangle_references) {
            document::ConstructionReference ref{face.instance_path,face.owner_id,face.semantic_key,0,true,"front",true};
            if(!document::placement_mesh::is_surface(ref,g))continue;
            benchmark(g,ref);found=true;break;
        }
        check(found,"Representative Part has no general source surface");
    }
    std::cout<<"Mesh placement, finite boundaries, alternatives, sides and persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
