#include "../app/workspace/sheet_cut_wire_preview.hpp"
#include <zima/document/part_document.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void quad(kernel::ViewerMesh& mesh,kernel::Vec3 a,kernel::Vec3 b,
          kernel::Vec3 c,kernel::Vec3 d,const kernel::FaceReference& reference) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),{a,b,c,d});
    mesh.triangles.insert(mesh.triangles.end(),{first,first+1,first+2,first,first+2,first+3});
    mesh.triangle_references.insert(mesh.triangle_references.end(),{reference,reference});
}
kernel::ViewerMesh plate(double z=2) {
    kernel::ViewerMesh mesh;
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->origin={0,0,z};
    kernel::FaceReference reference{"plate","side-a"};reference.surface=surface;
    reference.sheet_role=kernel::SheetFaceRole::SideA;reference.sheet_thickness=2;
    quad(mesh,{0,0,z},{10,0,z},{10,10,z},{0,10,z},reference);
    return mesh;
}
struct Fixture {
    document::PartDocument document;
    sketcher::Sketch sketch=sketcher::Sketch::create_default();
    document::HistoryContainer cut=document::PartDocument::create_extrusion_container(sketch.id);
    Fixture() {
        cut.extrusion.sheet_cut=true;cut.combine_mode=document::CombineMode::Subtract;
        cut.extrusion.length_forward=cut.extrusion.height=10;
    }
    std::vector<kernel::ViewerEdge> preview(const kernel::ViewerMesh& mesh) {
        document.sketches={sketch};
        const auto prism=document.extrusion_preview_edges(cut,mesh);
        return app::workspace_detail::estimated_sheet_cut_wire(cut,sketch,mesh,prism);
    }
};
void planar_boundaries() {
    Fixture f;static_cast<void>(f.sketch.add_rectangle(2,3,4,6));
    const auto mesh=plate();
    for(const bool clearance:{false,true}) {
        f.cut.extrusion.sheet_cut_clearance=clearance;
        const auto wire=f.preview(mesh);check(!wire.empty(),"No planar cut wire");
        bool top=false,bottom=false,connector=false;
        for(const auto& edge:wire) {
            check(edge.points.size()>1,"Empty cut edge");
            for(const auto p:edge.points) {
                check(p.z>=-1.e-7&&p.z<=2+1.e-7,"Wire escaped sheet thickness");
                check(std::min({std::abs(p.x-2),std::abs(p.x-4),std::abs(p.y-3),std::abs(p.y-6)})<1.e-6,
                      "Internal triangulation diagonal leaked into cut boundary");
                top|=std::abs(p.z-2)<1.e-7;bottom|=std::abs(p.z)<1.e-7;
            }
            connector|=std::abs(edge.points.front().z-edge.points.back().z)>1.9;
        }
        check(top&&bottom&&connector,"Wire does not show both skins and thickness");
    }
    f.cut.extrusion.length_forward=f.cut.extrusion.height=1;
    f.cut.extrusion.sheet_cut_clearance=false;
    check(f.preview(mesh).empty(),"By-surface wire offered an unreached Side A");
}
void holes_and_disconnected_sheets() {
    Fixture f;static_cast<void>(f.sketch.add_rectangle(2,2,8,8));
    static_cast<void>(f.sketch.add_rectangle(4,4,6,6));
    auto mesh=plate();auto remote=plate(20);
    const auto index=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),remote.vertices.begin(),remote.vertices.end());
    for(const auto v:remote.triangles)mesh.triangles.push_back(index+v);
    for(auto ref:remote.triangle_references){ref.owner_id="remote";mesh.triangle_references.push_back(ref);}
    for(const bool clearance:{false,true}) {
        f.cut.extrusion.sheet_cut_clearance=clearance;
        const auto wire=f.preview(mesh);bool inner=false,outer=false;
        for(const auto& edge:wire)for(const auto p:edge.points) {
            check(p.z<=2+1.e-6,"Wire reached a disconnected sheet beyond the limit");
            inner|=p.x>=4-1.e-6&&p.x<=6+1.e-6&&p.y>=4-1.e-6&&p.y<=6+1.e-6;
            outer|=std::abs(p.x-2)<1.e-6||std::abs(p.x-8)<1.e-6||std::abs(p.y-2)<1.e-6||std::abs(p.y-8)<1.e-6;
        }
        check(inner&&outer,"Wire lost the retained profile island");
    }
}
void curved_normals() {
    for(const double slope:{0.,.25,-.25})for(const bool inward_skin:{false,true}) {
        kernel::ViewerMesh mesh;
        auto surface=std::make_shared<kernel::SurfaceGeometry>();
        surface->kind=slope==0?kernel::SurfaceGeometry::Kind::Cylinder:kernel::SurfaceGeometry::Kind::Cone;
        surface->origin={0,10,0};surface->axis={1,0,0};surface->radial={0,-1,0};
        surface->radius=10;surface->semi_angle=std::atan(slope);
        kernel::FaceReference ref{"curved","side-a"};ref.surface=surface;
        ref.sheet_role=kernel::SheetFaceRole::SideA;ref.sheet_thickness=1;
        const auto point=[&](double x,double a){const double r=10+slope*x;return kernel::Vec3{x,10-r*std::cos(a),r*std::sin(a)};};
        for(int i=0;i<128;++i) {
            const double a=i*std::numbers::pi/256.,b=(i+1)*std::numbers::pi/256.;
            quad(mesh,point(0,a),point(10,a),point(10,b),point(0,b),ref);
        }
        if(inward_skin)for(std::size_t i=0;i<mesh.triangles.size();i+=3)
            std::swap(mesh.triangles[i+1],mesh.triangles[i+2]);
        Fixture f;static_cast<void>(f.sketch.add_rectangle(2,2,4,5));
        f.cut.extrusion.length_forward=f.cut.extrusion.height=30;
        const auto wire=f.preview(mesh);check(!wire.empty(),"No curved sheet wire");
        bool outer=false,inner=false;
        for(const auto& edge:wire)for(const auto p:edge.points) {
            const double depth=(inward_skin?-1:1)*(10+slope*p.x-std::hypot(p.y-10,p.z))/std::sqrt(1+slope*slope);
            check(depth>=-.01&&depth<=1.01,"Curved preview thickness went to the wrong side");
            outer|=std::abs(depth)<.01;inner|=std::abs(depth-1)<.01;
        }
        check(outer&&inner,"Curved cut wire omitted a sheet side");
    }
}
void wall_parallel_to_projection() {
    kernel::ViewerMesh mesh;auto surface=std::make_shared<kernel::SurfaceGeometry>();
    surface->origin={0,5,0};surface->axis={0,1,0};
    kernel::FaceReference ref{"parallel-wall","side-a"};ref.surface=surface;
    ref.sheet_role=kernel::SheetFaceRole::SideA;ref.sheet_thickness=2;
    quad(mesh,{0,5,1},{0,5,9},{10,5,9},{10,5,1},ref);
    Fixture f;static_cast<void>(f.sketch.add_rectangle(2,3,4,6));
    const auto wire=f.preview(mesh);check(!wire.empty(),"Parallel wall disappeared from the cut estimate");
    bool outer=false,inner=false;
    for(const auto& edge:wire)for(const auto p:edge.points) {
        check(p.x>=2-1.e-6&&p.x<=4+1.e-6&&p.z>=1-1.e-6&&p.z<=9+1.e-6,
              "Parallel wall preview escaped the selected profile or wall");
        check(p.y>=3-1.e-6&&p.y<=5+1.e-6,"Parallel wall preview reversed its thickness");
        outer|=std::abs(p.y-5)<1.e-6;inner|=std::abs(p.y-3)<1.e-6;
    }
    check(outer&&inner,"Parallel wall preview omitted full thickness");
}
void cache_invalidation() {
    Fixture f;static_cast<void>(f.sketch.add_rectangle(2,3,4,6));const auto mesh=plate();
    app::workspace_detail::SheetCutWirePreview cache;
    const auto append=[&](std::uint64_t generation=7) {
        f.document.sketches={f.sketch};
        auto wire=f.document.extrusion_preview_edges(f.cut,mesh);const auto original=wire.size();
        cache.append_estimate_or_current(f.cut,f.sketch,&mesh,generation,wire);
        check(wire.size()>original,"Cached preview omitted the second wire");
    };
    for(int i=0;i<50;++i)append();
    check(cache.estimate_evaluations==1,"Unchanged view updates recalculated the cut wire");
    f.cut.extrusion.length_forward=f.cut.extrusion.height=9;append();
    check(cache.estimate_evaluations==2,"Length edit retained stale cut wire");
    f.cut.extrusion.sheet_cut_clearance=true;append();
    check(cache.estimate_evaluations==3,"Method edit retained stale cut wire");
    static_cast<void>(f.sketch.add_rectangle(7,7,8,8));append();
    check(cache.estimate_evaluations==4,"Sketch edit retained stale cut wire");
    append(8);check(cache.estimate_evaluations==5,"Source geometry change retained stale cut wire");
    std::vector<kernel::ViewerEdge> incomplete;
    cache.append_estimate_or_current(f.cut,f.sketch,&mesh,8,incomplete);
    check(incomplete.empty()&&!cache.cache_valid&&cache.boundary_edges.empty(),"Incomplete profile left a stale cut wire");
    kernel::BodyResult saved;
    saved.mesh.edges.push_back({{{2,3,2},{2,3,0}},{f.cut.id,"generated-thickness-connection"}});
    saved.mesh.edges.push_back({{{0,0,0},{10,0,0}},{"unrelated-feature","edge"}});
    cache.assign(f.cut,f.sketch,saved,8);append(8);
    check(cache.estimate_evaluations==5&&cache.boundary_edges.size()==1,
          "Unchanged stored cut was estimated again or included unrelated geometry");
}
void bounded_preview_work() {
    using namespace app::workspace_detail;
    const auto cached_fallback=[](Fixture& fixture,const kernel::ViewerMesh& mesh,
                                  const std::vector<kernel::ViewerEdge>& prism) {
        check(!prism.empty(),"Budget test has no analytical prism");
        SheetCutWirePreview cache;
        for(int i=0;i<8;++i) {
            auto wire=prism;
            cache.append_estimate_or_current(fixture.cut,fixture.sketch,&mesh,17,wire);
            check(wire.size()==prism.size()&&std::equal(wire.begin(),wire.end(),prism.begin(),
                [](const auto& a,const auto& b){return a.points==b.points&&a.reference==b.reference;}),
                "Budget fallback changed the prism or retained a partial cut estimate");
        }
        check(cache.cache_valid&&cache.boundary_edges.empty()&&cache.estimate_evaluations==1,
              "Over-budget empty estimate was attempted repeatedly");
    };
    Fixture f;static_cast<void>(f.sketch.add_rectangle(2,3,4,6));f.document.sketches={f.sketch};
    const auto ordinary=plate();const auto prism=f.document.extrusion_preview_edges(f.cut,ordinary);
    check(!estimated_sheet_cut_wire(f.cut,f.sketch,ordinary,prism).empty(),"Budget fixture has no ordinary cut");

    // The first facets really intersect the cut. Most facets are irrelevant;
    // exceeding the scan budget must return no partial estimate and cache it.
    auto huge=ordinary;
    const auto remote=static_cast<std::uint32_t>(huge.vertices.size());
    huge.vertices.insert(huge.vertices.end(),{{1000,1000,2},{1010,1000,2},{1000,1010,2}});
    while(huge.triangle_references.size()<=sheet_cut_wire_detail::max_examined_facets) {
        huge.triangles.insert(huge.triangles.end(),{remote,remote+1,remote+2});
        huge.triangle_references.push_back(ordinary.triangle_references.front());
    }
    cached_fallback(f,huge,prism);

    auto oversized=prism;
    auto start=std::ranges::find_if(oversized,[](const auto& edge){return edge.reference.semantic_key=="preview:start";});
    check(start!=oversized.end(),"Budget fixture has no start rim");
    start->points.resize(sheet_cut_wire_detail::max_profile_points+1,start->points.front());
    cached_fallback(f,ordinary,oversized);
    oversized=prism;
    auto end=std::ranges::find_if(oversized,[](const auto& edge){return edge.reference.semantic_key=="preview:end";});
    check(end!=oversized.end(),"Budget fixture has no end rim");
    end->points.resize(sheet_cut_wire_detail::max_profile_points+1,end->points.front());
    cached_fallback(f,ordinary,oversized);

    kernel::ViewerMesh parallel;
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->origin={0,5,0};surface->axis={0,1,0};
    kernel::FaceReference reference{"slotted-wall","side-a"};reference.surface=surface;
    reference.sheet_role=kernel::SheetFaceRole::SideA;reference.sheet_thickness=2;
    quad(parallel,{0,5,1},{0,5,9},{200,5,9},{200,5,1},reference);
    Fixture slotted;
    for(int i=0;i<80;++i)static_cast<void>(slotted.sketch.add_rectangle(1+2*i,3,2+2*i,6));
    slotted.document.sketches={slotted.sketch};
    const auto slots_prism=slotted.document.extrusion_preview_edges(slotted.cut,parallel);
    cached_fallback(slotted,parallel,slots_prism);
}
}
int main() {
    try {planar_boundaries();holes_and_disconnected_sheets();curved_normals();wall_parallel_to_projection();cache_invalidation();bounded_preview_work();
        std::cout<<"Sheet Cut wire preview geometry passed without OCCT\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
