#include <zima/document/part_document.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/placement_exact_surface.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <numbers>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6){check(std::isfinite(a)&&std::abs(a-b)<tolerance,"Unexpected surface contact coordinate");}
bool indexed_resolve(document::Placement& p,const kernel::ViewerReferenceGeometry& g) {
    auto full=p;
    const bool expected=document::resolve_placement(full,g);
    document::PlacementReferenceGeometryIndex index(g);
    const auto selected=index.select(p.references);
    const auto bounded=document::PlacementReferenceGeometryIndex(g,p.references).select(p.references);
    check(document::serialize_viewer_reference_geometry(selected).dump()==
        document::serialize_viewer_reference_geometry(bounded).dump(),
        "Requested-reference index changed exact surface geometry or retained side");
    const bool actual=document::resolve_placement(p,selected);
    check(actual==expected && nlohmann::json(p).dump()==nlohmann::json(full).dump(),"Indexed solver changed placement or signed side");
    const kernel::Vec3 origin{p.x,p.y,p.z};
    check(document::point_constraint_remaining_dof(p.references,g,origin)==
          document::point_constraint_remaining_dof(p.references,selected,origin),"Indexed position degrees of freedom differ");
    check(document::orientation_constraint_remaining_dof(p.references,g,true,origin)==
          document::orientation_constraint_remaining_dof(p.references,selected,true,origin),"Indexed orientation degrees of freedom differ");
    check(nlohmann::json(document::placement_solution_branches(p,g)).dump()==
          nlohmann::json(document::placement_solution_branches(p,selected)).dump(),"Indexed solution branches differ");
    for(const auto& ref:p.references) {
        const auto a=document::measure_placement_reference_offset(ref,g,origin);
        const auto b=document::measure_placement_reference_offset(ref,selected,origin);
        check(a==b && (!a || std::signbit(*a)==std::signbit(*b)),"Indexed measurement or signed zero differs");
    }
    return actual;
}
document::ConstructionReference surface(){return {{},"patch","face",0,true,"front",true};}
void plane(kernel::ViewerReferenceGeometry& g,const char* id,kernel::Vec3 normal) {
    auto s=std::make_shared<kernel::SurfaceGeometry>();s->axis=normal;
    s->radial=normal.x?kernel::Vec3{0,1,0}:kernel::Vec3{1,0,0};
    g.triangle_references.push_back({id,"face",{},s});g.triangles.insert(g.triangles.end(),{0,1,2});
}
kernel::ViewerReferenceGeometry bowl(int count=20) {
    kernel::ViewerReferenceGeometry g;
    auto support=std::make_shared<kernel::ExactSurface>();
    support->kind=kernel::ExactSurface::Kind::Spline;
    support->u_degree=2;support->v_degree=1;support->u_count=3;support->v_count=2;
    support->u_knots={0,0,0,1,1,1};support->v_knots={0,0,1,1};
    support->poles={{-10,-5,100},{-10,5,100},{0,-5,-100},{0,5,-100},{10,-5,100},{10,5,100}};
    support->weights={1,1,1,1,1,1};
    auto exact=std::make_shared<kernel::ExactSurfaceGeometry>();exact->support=std::move(support);exact->validate();
    kernel::FaceReference face{"patch","face",{}};face.exact_surface=std::move(exact);
    for(int i=0;i<count;++i) {
        const double x=-10.+20.*i/count,next=-10.+20.*(i+1)/count;
        const auto base=static_cast<unsigned>(g.vertices.size());
        g.vertices.insert(g.vertices.end(),{{x,-5,x*x},{next,-5,next*next},{next,5,next*next},{x,5,x*x}});
        g.triangles.insert(g.triangles.end(),{base,base+1,base+2,base,base+2,base+3});
        g.triangle_references.insert(g.triangle_references.end(),2,face);
    }
    return g;
}
void constraints() {
    auto g=bowl();plane(g,"y",{0,1,0});plane(g,"z",{0,0,1});
    document::Placement p;p.x=2.4;p.y=1;p.z=5.76;p.references={surface()};
    check(indexed_resolve(p,g),"Exact surface first contact failed");near(p.x,2.4);near(p.y,1);near(p.z,5.76);
    check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==2,"Surface first contact must leave two translations");
    p.references.push_back({{},"y","face",1,true});
    check(indexed_resolve(p,g),"Surface and plane failed");
    p.references.push_back({{},"z","face",6,true});
    check(indexed_resolve(p,g),"Exact surface and two planes failed");near(p.x,std::sqrt(6.));
    check(document::point_constraint_remaining_dof(p.references,g,{p.x,p.y,p.z})==0,"Fully defined surface contact left a translation");
    const auto choices=document::placement_solution_branches(p,g);check(choices.size()==2,"Expected two distinct surface solutions");
    near(choices[0].x,-std::sqrt(6.));near(choices[1].x,std::sqrt(6.));
    for(auto chosen:choices) {
        chosen=nlohmann::json(chosen).get<document::Placement>();
        auto stored=document::load_viewer_reference_geometry(document::serialize_viewer_reference_geometry(g));
        check(indexed_resolve(chosen,stored),"Saved surface solution failed");
        near(chosen.x,chosen.x<0?-std::sqrt(6.):std::sqrt(6.));
        // Reorder complete triangles: topology identity and selected root must survive.
        for(std::size_t i=0;i<20;++i)for(int c=0;c<3;++c)std::swap(stored.triangles[i*3+c],stored.triangles[(39-i)*3+c]);
        const auto x=chosen.x;check(indexed_resolve(chosen,stored),"Reordered mesh failed");near(chosen.x,x);
    }
    const auto before=p;p.references.back().offset=-1;
    check(!indexed_resolve(p,g)&&!p.reference_valid,"Impossible surface equations accepted");near(p.x,before.x);near(p.z,before.z);
    p=before;p.references[1].offset=6;
    check(!indexed_resolve(p,g),"Surface extrapolated outside trimmed boundary");
    p=before;p.references.push_back({{},"y","face",2,true});
    check(!indexed_resolve(p,g),"Contradictory reference accepted");
    p=before;p.references.front().instance_path="other-occurrence";
    check(!indexed_resolve(p,g),"Reference resolved another occurrence's surface");
    auto changed=bowl();for(auto& vertex:changed.vertices)vertex.z+=2;
    kernel::ExactSurfaceTransformCache shift;
    for(auto& face:changed.triangle_references)shift.apply(face.exact_surface,
        [](auto p){p.z+=2;return p;},[](auto v){return v;});
    plane(changed,"y",{0,1,0});plane(changed,"z",{0,0,1});p=before;
    check(indexed_resolve(p,changed),"Changed source did not resolve");near(p.x,2);near(p.z,6);
    kernel::ViewerAxis axis;axis.point={2.4,1,100};axis.direction={0,0,1};axis.reference={"vertical","axis",{}};g.axes.push_back(axis);
    p=before;p.references={surface(),{{},"vertical","axis"}};
    check(indexed_resolve(p,g),"General surface and straight axis failed");near(p.x,2.4);near(p.y,1);near(p.z,5.76);
    g.points.push_back({{2.4,1,5.76},{"anchor","point",{}}});
    p.references={surface(),{{},"anchor","point"}};
    check(indexed_resolve(p,g),"General surface and point failed");near(p.x,2.4);near(p.z,5.76);
    // A datum plane remains unbounded and exact, independent of displayed triangles.
    p={};p.x=100;p.y=99;p.references={{{},"z","face",7,true}};
    check(indexed_resolve(p,g),"Analytic plane regressed");near(p.x,100);near(p.y,99);near(p.z,7);
}
void transformed() {
    auto g=bowl();for(auto& v:g.vertices)v={20-v.y,v.x-10,v.z+7};
    kernel::ExactSurfaceTransformCache moved;
    for(auto& face:g.triangle_references)moved.apply(face.exact_surface,
        [](auto p){return kernel::Vec3{20-p.y,p.x-10,p.z+7};},
        [](auto v){return kernel::Vec3{-v.y,v.x,v.z};});
    for(auto& face:g.triangle_references)face.instance_path="nested/instance";
    plane(g,"x",{1,0,0});plane(g,"z",{0,0,1});
    auto ref=surface();ref.instance_path="nested/instance";
    document::Placement p;p.x=19;p.y=-7.6;p.z=13;
    p.references={ref,{{},"x","face",19,true},{{},"z","face",13,true}};
    check(indexed_resolve(p,g),"Transformed occurrence failed");near(p.x,19);near(p.y,std::sqrt(6.)-10);near(p.z,13);
    check(document::placement_solution_branches(p,g).size()==2,"Transformed occurrence lost alternatives");
}
void tessellation_and_packet() {
    for(int density:{1,2,17,1000}) {
        auto g=bowl(density);plane(g,"y",{0,1,0});plane(g,"z",{0,0,1});
        document::Placement p;p.x=-2.4;p.y=1;p.z=6;
        p.references={surface(),{{},"y","face",1,true},{{},"z","face",6,true}};
        check(document::resolve_placement(p,g),"Tessellation density changed solvability");
        near(p.x,-std::sqrt(6.));near(p.y,1);near(p.z,6);
        for(auto& vertex:g.vertices)vertex={1000,-2000,3000};
        check(document::resolve_placement(p,g),"Display coordinates became mathematical inputs");
        near(p.x,-std::sqrt(6.));near(p.z,6);
    }
    auto g=bowl(2);auto separate=std::make_shared<kernel::ExactSurfaceGeometry>(*g.triangle_references[0].exact_surface);
    separate->u_max=.4;
    g.triangle_references[2].exact_surface=g.triangle_references[3].exact_surface=separate;
    kernel::BodyResult packet;
    packet.mesh.vertices=g.vertices;packet.mesh.triangles=g.triangles;
    packet.mesh.triangle_references=g.triangle_references;packet.mesh.original_references=g;
    auto encoded=document::serialize_body_result(packet);
    const auto reopened=document::load_body_result(encoded);
    check(document::serialize_body_result(reopened)==encoded,"Body packet exact surface round-trip changed data");
    const auto& faces=reopened.mesh.original_references.triangle_references;
    check(faces.size()==4&&faces[0]==faces[2]&&
        faces[0].exact_surface->u_max==1&&faces[2].exact_surface->u_max==.4,
        "Distinct patches of one semantic face lost their exact trims");
    for(bool contour_only:{false,true}) {
        auto analytic=g;
        auto first=std::make_shared<kernel::SurfaceGeometry>();
        first->kind=kernel::SurfaceGeometry::Kind::Cylinder;
        first->radius=3;first->axis={0,1,0};first->axial_max=20;
        auto second=std::make_shared<kernel::SurfaceGeometry>(*first);
        second->origin={5,7,9};second->axis={-1,0,0};second->reversed=true;
        for(std::size_t i=0;i<analytic.triangle_references.size();++i) {
            auto& face=analytic.triangle_references[i];face.exact_surface.reset();
            face.surface.reset();face.contour_surface.reset();
            (contour_only?face.contour_surface:face.surface)=i<2?first:second;
        }
        for(int repeat=0;repeat<3;++repeat) {
            analytic=document::load_viewer_reference_geometry(
                document::serialize_viewer_reference_geometry(analytic));
            const auto& a=analytic.triangle_references.at(0);
            const auto& b=analytic.triangle_references.at(2);
            check(a==b&&*(contour_only?a.contour_surface:a.surface)==*first&&
                *(contour_only?b.contour_surface:b.surface)==*second,
                "Analytic patches sharing an identity lost their support or side on reopening");
        }
    }
}
void sides() {
    for(bool reverse:{false,true})for(bool flip:{false,true})for(double offset:{0.,-0.,2.,-2.}) {
        auto g=bowl();if(reverse)for(std::size_t i=0;i<g.triangles.size();i+=3)std::swap(g.triangles[i+1],g.triangles[i+2]);
        if(reverse) {
            auto exact=std::make_shared<kernel::ExactSurfaceGeometry>(*g.triangle_references.front().exact_surface);
            exact->reversed=true;for(auto& face:g.triangle_references)face.exact_surface=exact;
        }
        auto ref=surface();ref.offset=offset;ref.flip=flip;
        const double sign=(reverse?-1.:1.)/std::sqrt(24.04);
        const kernel::Vec3 normal{-4.8*sign,0,sign};
        const kernel::Vec3 anchor{2.4+normal.x*offset,1,5.76+normal.z*offset};
        document::Placement p;p.x=anchor.x;p.y=anchor.y;p.z=anchor.z;p.references={ref};
        // Large offsets may have a closer patch; the exact seeded patch still has zero residual.
        check(indexed_resolve(p,g),"Offset surface contact failed");near(p.x,anchor.x);near(p.y,anchor.y);near(p.z,anchor.z);
        const double r=std::numbers::pi/180,cx=std::cos(p.rotation_x*r),sx=std::sin(p.rotation_x*r),
            cy=std::cos(p.rotation_y*r),sy=std::sin(p.rotation_y*r),cz=std::cos(p.rotation_z*r),sz=std::sin(p.rotation_z*r);
        const kernel::Vec3 front{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx};
        near(front.x*normal.x+front.y*normal.y+front.z*normal.z,flip?-1:1);
        const auto stored=nlohmann::json(p).get<document::Placement>();
        check(stored.references[0].flip==flip&&std::signbit(stored.references[0].offset)==std::signbit(offset),"Surface side or signed zero lost");
        document::ConstructionObject point;point.kind=document::ConstructionKind::Point;
        point.origin=anchor;point.references={ref};
        check(document::resolve_construction(point,g),"Construction Point exact surface normal unresolved");
        near(point.origin.x,p.x);near(point.origin.y,p.y);near(point.origin.z,p.z);
        near(point.rotation.x,p.rotation_x);near(point.rotation.y,p.rotation_y);near(point.rotation.z,p.rotation_z);
        const auto retained=point;
        check(!document::resolve_construction(point,{}),"Missing construction source remained valid");
        near(point.origin.x,retained.origin.x);near(point.origin.y,retained.origin.y);near(point.origin.z,retained.origin.z);
        near(point.rotation.x,retained.rotation.x);near(point.rotation.y,retained.rotation.y);near(point.rotation.z,retained.rotation.z);
    }
}
void benchmark(const kernel::ViewerReferenceGeometry& g,const document::ConstructionReference& ref) {
    document::Placement p;p.x=2;p.y=1;p.z=4;p.references={ref};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<30;++i)check(document::resolve_placement(p,g),"Repeated mesh placement failed");
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;
    std::cout<<"Surface contact: "<<g.triangle_references.size()<<" reference triangles, mean "<<ms<<" ms\n";
    check(ms<500,"Surface contact exceeded interactive time budget");
}
void indexed_benchmark(const kernel::ViewerReferenceGeometry& g,const document::ConstructionReference& ref) {
    auto start=std::chrono::steady_clock::now();
    document::PlacementReferenceGeometryIndex index(g);
    const double indexing=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    document::Placement seed;seed.x=2;seed.y=1;seed.z=4;seed.references={ref};
    start=std::chrono::steady_clock::now();const auto selected=index.select(seed.references);
    const double selecting=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    auto full=seed,compact=seed;
    check(document::resolve_placement(full,g)==document::resolve_placement(compact,selected) &&
          nlohmann::json(full).dump()==nlohmann::json(compact).dump(),"Measured indexed solve differs");
    const auto timing=[&](const auto& geometry) {
        const auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<10;++i){auto p=seed;static_cast<void>(document::resolve_placement(p,geometry));}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/10;
    };
    std::cout<<"Index benchmark source_triangles="<<g.triangle_references.size()<<" selected_triangles="<<selected.triangle_references.size()
        <<" build_ms="<<indexing<<" select_ms="<<selecting<<" full_solve_ms="<<timing(g)<<" indexed_solve_ms="<<timing(selected)<<std::endl;
}
void elliptical_fixed_points() {
    auto generator=std::make_shared<kernel::ExactSurfaceGenerator>();
    generator->kind=kernel::ExactSurfaceGenerator::Kind::Ellipse;
    generator->radius=40;generator->minor_radius=30;
    auto support=std::make_shared<kernel::ExactSurface>();
    support->kind=kernel::ExactSurface::Kind::Extrusion;support->generator=generator;
    for(double angle:{.16,.6,1.2,1.971363,2.3,3.7,5.9})for(bool reverse:{false,true})
        for(double offset:{0.,-0.,1.,-1.}) {
            auto patch=std::make_shared<kernel::ExactSurfaceGeometry>();
            patch->support=support;patch->u_max=2*std::numbers::pi;patch->v_max=120;patch->reversed=reverse;
            patch->validate();
            kernel::ViewerReferenceGeometry geometry;
            kernel::FaceReference face{"patch","face",{}};face.exact_surface=patch;
            geometry.triangle_references.push_back(face);
            const auto base=kernel::evaluate_exact_surface(*patch,angle,24.886815218257652,offset);
            document::Placement placed;placed.x=base.point.x;placed.y=base.point.y;placed.z=base.point.z;
            placed.references={surface()};placed.references.front().offset=offset;
            check(document::resolve_placement(placed,geometry),"Elliptical fixed point did not resolve");
            const auto snapshot=nlohmann::json(placed).dump();
            for(int pass=0;pass<12;++pass) {
                check(document::resolve_placement(placed,geometry),"Repeated elliptical fixed point did not resolve");
                check(nlohmann::json(placed).dump()==snapshot,"Repeated surface resolution drifted from a valid fixed point");
            }
            // The fixed-point rule must still follow a genuine source change.
            auto changed=std::make_shared<kernel::ExactSurfaceGeometry>(*patch);
            changed->origin.x+=.0001;
            geometry.triangle_references.front().exact_surface=changed;
            check(document::resolve_placement(placed,geometry),"Changed ellipse source did not resolve");
            check(std::hypot(placed.x-base.point.x,placed.y-base.point.y)>1e-7,
                "Fixed-point reuse ignored changed source mathematics");
            if(offset==0)near((placed.x-.0001)*(placed.x-.0001)/1600+placed.y*placed.y/900,1.,1e-12);
        }
}
}
int main(){try {
    if(const char* path=std::getenv("ZIMA_VERIFY_EXACT_SURFACE_SETTLING")) {
        auto part=document::PartDocument::load(path);
        kernel::OcctKernel engine;
        for(int pass=0;pass<10;++pass) {
            const auto before=part.serialized();
            const auto calculated=engine.evaluate_history(part.kernel_operations());
            check(!calculated.empty(),"Settling fixture has no calculated geometry");
            part.resolve_constructions(calculated.back().mesh.original_references);
            const auto after=part.serialized();
            const auto difference=nlohmann::json::diff(before,after);
            std::cout<<"Exact surface settling pass "<<pass<<": "<<difference.dump()<<std::endl;
            if(difference.empty())return 0;
        }
        throw std::runtime_error("Repeated exact surface resolution did not settle");
    }
    constraints();sides();transformed();tessellation_and_packet();elliptical_fixed_points();benchmark(bowl(10000),surface());
    indexed_benchmark(bowl(),surface());
    if(const char* path=std::getenv("ZIMA_VERIFY_EXACT_SURFACE_SOURCE")) {
        std::vector<kernel::BodyResult> results;auto part=document::PartDocument::load(path,&results);
        check(!results.empty(),"Representative Part has no saved reference geometry");
        const auto& g=results.back().mesh.original_references;
        if(const char* key=std::getenv("ZIMA_VERIFY_EXACT_SURFACE_FACE")) {
            const auto face=std::ranges::find(g.triangle_references,std::string(key),&kernel::FaceReference::semantic_key);
            check(face!=g.triangle_references.end(),"Measured face is missing");
            indexed_benchmark(g,{face->instance_path,face->owner_id,face->semantic_key,0,true,"front",true});
            std::cout<<"Representative indexed placement passed\n";return 0;
        }
        bool found=false;
        for(const auto& face:g.triangle_references) {
            document::ConstructionReference ref{face.instance_path,face.owner_id,face.semantic_key,0,true,"front",true};
            if(!document::placement_exact::is_surface(ref,g))continue;
            benchmark(g,ref);found=true;break;
        }
        check(found,"Representative Part has no general source surface");
    }
    std::cout<<"Surface placement, finite boundaries, alternatives, sides and persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
