#include "profile_solid_fixture.hpp"
#include "../app/fem_linear_statics.hpp"
#include <zima/workspace/model_calculation.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/document/fem_presentation.hpp>
#include <zima/workspace/fem_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <iostream>
#include <numbers>
#include <numeric>

using namespace zima;
using kernel::Vec3;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
void near(Vec3 actual,Vec3 expected,double tolerance,const char* message){
    if(std::hypot(actual.x-expected.x,actual.y-expected.y,actual.z-expected.z)>tolerance){
        std::cerr<<message<<": actual "<<actual.x<<','<<actual.y<<','<<actual.z
            <<" expected "<<expected.x<<','<<expected.y<<','<<expected.z<<'\n';throw std::runtime_error(message);
    }
}
document::FemCondition pressure(){
    document::FemCondition c;c.id="pressure";c.name="Pressure";
    c.kind=document::FemConditionKind::Pressure;c.region=document::FemRegionKind::Surface;
    c.pressure_MPa=2.5;c.placement.reference_valid=true;return c;
}
void all_fixed(app::FemVolumeMesh& mesh,document::FemDefinition& definition){
    document::FemCondition fixed;fixed.id="all-fixed";fixed.name="Fixed";
    app::FemMeshConditionRegion group{document::FemRegionKind::Point,{},{}};
    group.nodes.resize(mesh.surface.vertices.size());std::iota(group.nodes.begin(),group.nodes.end(),0);
    mesh.condition_regions[fixed.id]=std::move(group);definition.conditions.push_back(fixed);
}
void balance(const app::FemVolumeMesh& mesh,const app::FemStaticResult& result,Vec3 force,Vec3 moment,double tolerance=1e-6){
    Vec3 reaction{},torque{};
    for(std::size_t i=0;i<result.reaction.size();++i){
        reaction=add(reaction,result.reaction[i]);torque=add(torque,cross(mesh.surface.vertices[i],result.reaction[i]));
    }
    near(reaction,mul(force,-1),tolerance,"Pressure reaction imbalance");
    near(torque,mul(moment,-1),tolerance*20,"Pressure moment imbalance");
}
kernel::FaceReference face_at(const kernel::ViewerMesh& mesh,unsigned axis,double coordinate){
    for(std::size_t t=0;t<mesh.triangle_references.size();++t){
        bool match=true;
        for(unsigned i=0;i<3;++i){const auto p=mesh.vertices[mesh.triangles[3*t+i]];
            match&=std::abs((axis==0?p.x:axis==1?p.y:p.z)-coordinate)<1e-7;}
        if(match)return mesh.triangle_references[t];
    }
    throw std::runtime_error("Pressure fixture face missing");
}
void set_face(document::FemCondition& c,kernel::FaceReference f){c.references={{f.owner_id,f.semantic_key,f.instance_path}};}
void single_element(){
    for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic})
    for(bool reversed:{false,true})for(bool winding:{false,true}){
        app::FemVolumeMesh mesh;mesh.order=order;
        mesh.surface.vertices={{0,0,0},{2,0,0},{0,3,0},{0,0,4},
            {1,0,0},{1,1.5,0},{0,1.5,0},{0,0,2},{0,1.5,2},{1,0,2}};
        if(order==document::FemElementOrder::Linear)mesh.surface.vertices.resize(4);
        mesh.tetrahedra.push_back({0,1,2,3,4,5,6,7,8,9});
        std::array<std::uint32_t,6> face=winding?std::array<std::uint32_t,6>{0,2,1,6,5,4}:
            std::array<std::uint32_t,6>{0,1,2,4,5,6};
        mesh.condition_regions["pressure"]={document::FemRegionKind::Surface,{}, {face}};
        auto c=pressure();c.pressure_reversed=reversed;
        document::FemDefinition def;def.conditions={c};all_fixed(mesh,def);
        const double sign=reversed?-1:1;
        auto result=app::solve_fem_linear_statics(mesh,def,{210000,.3});
        const Vec3 force{0,0,sign*7.5};balance(mesh,result,force,cross({2./3,1,0},force));
        for(unsigned i=0;i<3;++i)near(result.reaction[i],mul(force,order==document::FemElementOrder::Linear?-1./3:0.),1e-10,
            "Incorrect consistent corner load");
        if(order==document::FemElementOrder::Quadratic){
            for(unsigned i=4;i<7;++i)near(result.reaction[i],mul(force,-1./3),1e-10,"Incorrect quadratic midside load");
            // Exact graph z=k*x*y over x/2+y/3<=1. Its vector area and
            // first moments below are analytical polynomial integrals.
            constexpr double k=.1;mesh.surface.vertices[5].z=1.5*k;
            result=app::solve_fem_linear_statics(mesh,def,{210000,.3});
            const auto force_curved=mul({-3*k,-2*k,3},sign*c.pressure_MPa);
            const auto moment=mul({3+1.2*k*k,-2-1.8*k*k,2.5*k},sign*c.pressure_MPa);
            balance(mesh,result,force_curved,moment);
        }
        const auto rejected=[&](auto changed_mesh,auto changed_definition,const char* expected){
            bool failed=false;
            try{static_cast<void>(app::solve_fem_linear_statics(changed_mesh,changed_definition,{210000,.3}));}
            catch(const std::runtime_error& error){failed=std::string_view(error.what())==expected;}
            check(failed,"Invalid pressure was not rejected with its specific diagnostic");
        };
        auto invalid=def;invalid.conditions[0].pressure_MPa=-1;
        rejected(mesh,invalid,"Invalid FEM condition value.");
        invalid=def;invalid.conditions[0].placement.reference_valid=false;
        rejected(mesh,invalid,"The condition placement is unresolved.");
        invalid=def;invalid.conditions[0].region=document::FemRegionKind::Curve;
        rejected(mesh,invalid,"Pressure requires a surface region.");
        auto missing=mesh;missing.condition_regions.erase(c.id);
        rejected(missing,def,"The FEM load has no mesh region.");
        missing=mesh;missing.tetrahedra.push_back(mesh.tetrahedra.front());
        rejected(missing,def,"The FEM pressure region has no unambiguous surface normal.");
    }
    std::cout<<"Pressure single-element: linear/quadratic, reversed winding/direction, curved vector area/moment and consistent nodal loads passed\n";
}
void stress_fields(){
    for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic})
    for(bool curved:{false,true})for(bool rotated:{false,true})for(int mode=0;mode<5;++mode) {
        if(curved&&order==document::FemElementOrder::Linear)continue;
        app::FemVolumeMesh mesh;mesh.order=order;
        mesh.surface.vertices={{0,0,0},{2,0,0},{0,3,0},{0,0,4},
            {1,0,0},{1,1.5,0},{0,1.5,0},{0,0,2},{0,1.5,2},{1,0,2}};
        if(order==document::FemElementOrder::Linear)mesh.surface.vertices.resize(4);
        if(curved)mesh.surface.vertices[4].z=.12;
        mesh.tetrahedra.push_back({0,1,2,3,4,5,6,7,8,9});
        const app::FemElasticMaterial material{210000,.3};
        const double E=material.young_MPa,nu=material.poisson,G=E/(2*(1+nu)),lambda=2*G*nu/(1-2*nu);
        std::array<std::array<double,3>,3> A{};
        if(mode==0){A[0][1]=-.003;A[1][0]=.003;}
        if(mode==1){A[0][0]=.001;A[1][1]=A[2][2]=-nu*.001;}
        if(mode==2)A[0][0]=A[1][1]=A[2][2]=.001;
        if(mode==3)A[0][1]=.002;
        if(mode==4)A={{{.001,.0002,-.0004},{.0006,-.0003,.0007},{.0009,-.0005,.0002}}};
        std::array<std::array<double,3>,3> expected{};
        const double trace=A[0][0]+A[1][1]+A[2][2];
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
            expected[i][j]=G*(A[i][j]+A[j][i])+(i==j?lambda*trace:0);
        const auto rotate=[&](Vec3 v){const double c=rotated?std::cos(.73):1,s=rotated?std::sin(.73):0;return Vec3{c*v.x-s*v.y,s*v.x+c*v.y,v.z};};
        std::vector<Vec3> displacement;
        for(auto& p:mesh.surface.vertices){
            const double v[3]{p.x,p.y,p.z};Vec3 u{};double* dst[3]{&u.x,&u.y,&u.z};
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)*dst[i]+=A[i][j]*v[j];
            displacement.push_back(add(rotate(u),{.1,-.2,.3}));p=rotate(p);
        }
        std::array<std::array<double,3>,3> R{};
        for(unsigned j=0;j<3;++j){Vec3 e{};(j==0?e.x:j==1?e.y:e.z)=1;const auto r=rotate(e);R[0][j]=r.x;R[1][j]=r.y;R[2][j]=r.z;}
        std::array<std::array<double,3>,3> transformed{};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)
            transformed[i][j]+=R[i][a]*expected[a][b]*R[j][b];
        const auto result=app::fem_element_stresses(mesh,displacement,material).at(0);
        const std::array<double,6> exact{transformed[0][0],transformed[1][1],transformed[2][2],transformed[0][1],transformed[1][2],transformed[2][0]};
        for(unsigned i=0;i<6;++i)check(std::abs(result.components_MPa[i]-exact[i])<1e-8,"Recovered stress tensor is incorrect");
        if(mode==0||mode==2)check(result.von_mises_MPa<1e-8,"Rigid motion or hydrostatic stress has nonzero von Mises stress");
        if(mode==1)check(std::abs(result.von_mises_MPa-E*.001)<1e-8,"Uniaxial equivalent stress is incorrect");
        if(mode==3)check(std::abs(result.von_mises_MPa-std::sqrt(3.)*G*.002)<1e-8,"Shear equivalent stress is incorrect");
    }
    {
        app::FemVolumeMesh mesh;mesh.order=document::FemElementOrder::Quadratic;
        mesh.surface.vertices={{0,0,0},{2,0,0},{0,3,0},{0,0,4},
            {1,0,0},{1,1.5,0},{0,1.5,0},{0,0,2},{0,1.5,2},{1,0,2}};
        mesh.tetrahedra.push_back({0,1,2,3,4,5,6,7,8,9});
        std::vector<Vec3> u;for(const auto p:mesh.surface.vertices)u.push_back({.001*p.x*p.x,0,0});
        const auto stress=app::fem_element_stresses(mesh,u,{210000,.3}).at(0);
        const double G=210000/(2*1.3),lambda=2*G*.3/.4;
        check(std::abs(stress.components_MPa[0]-(lambda+2*G)*.001)<1e-8&&
            std::abs(stress.components_MPa[1]-lambda*.001)<1e-8&&
            std::abs(stress.von_mises_MPa-2*G*.001)<1e-8,"Quadratic varying stress did not use its volume average");
    }
    std::cout<<"Stress recovery: linear/quadratic, curved geometry, rigid motion, axial/hydrostatic/shear/general states, rotated frames and varying quadratic stress passed\n";
}
void generated_regions(){
    kernel::OcctKernel engine;auto doc=document::PartDocument::create_default();
    const auto feature=test::rectangular_feature(doc,{20,10,5});doc.history={feature};
    const auto solid=workspace::calculate_part_with_resolved_references(engine,doc).back();
    const auto calculations=kernel::body_calculation_count.load();
    for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic}){
        document::FemDefinition base;base.mesh.maximum_size_mm=3;base.mesh.minimum_size_mm=.1;base.mesh.order=order;
        for(unsigned axis=0;axis<3;++axis)for(double sign:{-1.,1.}){
            auto c=pressure();const double half=axis==0?10:axis==1?5:2.5;
            set_face(c,face_at(solid.mesh,axis,sign*half));
            auto def=base;def.conditions={c};auto mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
            Vec3 normal{};if(axis==0)normal.x=sign;else if(axis==1)normal.y=sign;else normal.z=sign;
            for(bool reversed:{false,true}){
                def.conditions[0].pressure_reversed=reversed;
                const auto force=mul(normal,(reversed?1:-1)*c.pressure_MPa*(axis==0?50:axis==1?100:200));
                balance(mesh,app::solve_fem_linear_statics(mesh,def,{210000,.3}),force,{});
            }
        }
        for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
        for(double level:{-1.,0.,1.}){
            const auto normal=plane==sketcher::SketchPlane::XY?Vec3{0,0,1}:
                plane==sketcher::SketchPlane::XZ?Vec3{0,1,0}:Vec3{1,0,0};
            auto c=pressure();auto sketch=sketcher::Sketch::create_default();
            sketch.owner_container_id=c.id;sketch.plane_auto=false;sketch.plane=plane;
            sketch.plane_offset=level*(plane==sketcher::SketchPlane::XY?2.5:plane==sketcher::SketchPlane::XZ?5:10);
            static_cast<void>(sketch.add_rectangle(-1,-1,2,1));
            c.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{},{},false};
            auto def=base;def.conditions={c};auto mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
            const Vec3 center=plane==sketcher::SketchPlane::YZ?Vec3{sketch.plane_offset,.5,0}:
                plane==sketcher::SketchPlane::XZ?Vec3{.5,sketch.plane_offset,0}:Vec3{.5,0,sketch.plane_offset};
            for(bool reversed:{false,true}){
                def.conditions[0].pressure_reversed=reversed;
                const auto force=mul(normal,(reversed?1:-1)*c.pressure_MPa*6);
                balance(mesh,app::solve_fem_linear_statics(mesh,def,{210000,.3}),force,cross(center,force));
            }
        }
        for(double sign:{-1.,1.}){
            auto c=pressure();set_face(c,face_at(solid.mesh,2,sign*2.5));
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=c.id;
            sketch.plane_auto=false;sketch.plane_offset=sign*4;
            static_cast<void>(sketch.add_rectangle(-1,-1,3,1));
            c.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{{1,0,sign*2.5}}};
            auto def=base;def.conditions={c};auto mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
            const Vec3 force{0,0,-sign*20};
            balance(mesh,app::solve_fem_linear_statics(mesh,def,{210000,.3}),force,cross({1,0,sign*2.5},force));
        }
        {
            // A bounded region with a hole must not load the excluded center.
            auto c=pressure();auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=c.id;
            sketch.plane_auto=false;sketch.plane_offset=2.5;
            static_cast<void>(sketch.add_circle(0,0,2));static_cast<void>(sketch.add_circle(0,0,1));
            c.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{},{},false};
            auto def=base;def.mesh.maximum_size_mm=.6;def.conditions={c};
            auto mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
            const auto result=app::solve_fem_linear_statics(mesh,def,{210000,.3});
            const double expected=3*std::numbers::pi*c.pressure_MPa;
            const double tolerance=expected*(order==document::FemElementOrder::Linear?.025:.0005);
            balance(mesh,result,{0,0,-expected},{},tolerance);
            for(std::size_t i=0;i<mesh.surface.vertices.size();++i){const auto p=mesh.surface.vertices[i];
                if(std::hypot(p.x,p.y)<.8)near(result.reaction[i],{},1e-12,"Sketch hole received pressure");}
        }
        // Constant axial stress is an exact linear displacement patch test
        // for both orders when nu=0 and the opposite cap is clamped.
        auto c=pressure();set_face(c,face_at(solid.mesh,2,2.5));
        auto clamp=c;clamp.id="clamp";clamp.kind=document::FemConditionKind::Constraint;
        set_face(clamp,face_at(solid.mesh,2,-2.5));
        auto def=base;def.conditions={c,clamp};
        std::vector<app::FemProgress> mesh_progress;
        const auto mesh=app::generate_fem_volume_mesh(solid,def,[&](const auto& progress){mesh_progress.push_back(progress);});
        check(mesh_progress.size()>=4&&mesh_progress.front().phase==app::FemProgressPhase::PreparingGeometry&&
            mesh_progress.back().phase==app::FemProgressPhase::ReadingMesh,"Mesh progress is incomplete");
        check(std::ranges::any_of(mesh_progress,[](const auto& p){return p.phase==app::FemProgressPhase::GeneratingMesh&&p.total==0;}),
            "Meshing must report its unbounded phase without an invented percentage");
        for(double value:{2.5,0.,5.,1.25})for(bool reversed:{false,true}){
            def.conditions[0].pressure_MPa=value;def.conditions[0].pressure_reversed=reversed;
            std::vector<app::FemProgress> progress;
            const auto result=app::solve_fem_linear_statics(mesh,def,{210000,0},[&](const auto& p){progress.push_back(p);});
            const auto without_progress=app::solve_fem_linear_statics(mesh,def,{210000,0});
            check(result.displacement==without_progress.displacement&&result.reaction==without_progress.reaction&&
                result.element_von_mises_MPa==without_progress.element_von_mises_MPa&&
                result.maximum_displacement_mm==without_progress.maximum_displacement_mm&&
                result.maximum_von_mises_MPa==without_progress.maximum_von_mises_MPa,
                "Progress reporting changed numerical results");
            for(const auto& p:progress)check(!p.total||p.completed<=p.total,"Progress count exceeds actual work");
            for(auto phase:{app::FemProgressPhase::AssemblingSystem,app::FemProgressPhase::ComputingResults})
                check(std::ranges::any_of(progress,[&](const auto& p){return p.phase==phase&&
                    p.total==mesh.tetrahedra.size()&&p.completed==p.total;}),"Element progress did not finish its real work count");
            std::size_t iteration=0;
            for(const auto& p:progress)if(p.phase==app::FemProgressPhase::SolvingSystem) {
                check(!p.total&&p.completed==iteration++,"Solver progress invents a percentage or skips an iteration");
            }
            check(iteration>0&&(value==0||iteration>1),"Solver iteration progress is missing");
            check(result.element_von_mises_MPa.size()==mesh.tetrahedra.size(),"Missing element stresses");
            for(const double stress:result.element_von_mises_MPa)check(std::abs(stress-value)<1e-5,"Solved axial stress is incorrect");
            check(mesh.surface_elements.size()==mesh.surface.triangles.size()/3,"Surface stress ownership is incomplete");
            for(std::size_t t=0;t<mesh.surface_elements.size();++t) {
                const auto& cell=mesh.tetrahedra.at(mesh.surface_elements[t]);
                for(unsigned c=0;c<3;++c)check(std::ranges::find(cell,mesh.surface.triangles[t*3+c])!=cell.end(),"Surface triangle has the wrong stress owner");
            }
            const double traction=(reversed?1:-1)*value;
            balance(mesh,result,{0,0,traction*200},{},1e-4);
            for(std::size_t i=0;i<result.displacement.size();++i)
                near(result.displacement[i],{0,0,traction*(mesh.surface.vertices[i].z+2.5)/210000},1e-10,
                    "Pressure displacement failed the constant-stress patch test");
        }
        // Simultaneous pressure and a signed total Force must superpose.
        auto force=c;force.id="force";force.kind=document::FemConditionKind::Force;force.force_N=175;
        def.conditions={c,clamp,force};auto combined=mesh;
        combined.condition_regions[force.id]=mesh.condition_regions.at(c.id);
        balance(combined,app::solve_fem_linear_statics(combined,def,{210000,0}),{0,0,-325},{},1e-4);
    }
    check(kernel::body_calculation_count.load()==calculations,"Pressure mesh/solve recalculated CAD");
    std::cout<<"Pressure generated box: six outward faces, both signs, three Sketch planes at boundaries/interior, projected patches, repeated values, exact displacement and Force superposition passed\n";
}
void curved_regions(){
    kernel::OcctKernel engine;auto doc=document::PartDocument::create_default();
    doc.history={test::circular_feature(doc,5,4)};
    const auto solid=workspace::calculate_part_with_resolved_references(engine,doc).back();
    const auto bottom=face_at(solid.mesh,2,0),top=face_at(solid.mesh,2,4);
    kernel::FaceReference wall;
    for(const auto& f:solid.mesh.triangle_references)
        if(f.semantic_key!=bottom.semantic_key&&f.semantic_key!=top.semantic_key){wall=f;break;}
    check(wall.valid(),"Cylinder wall missing");
    for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic}){
        document::FemDefinition def;def.mesh.maximum_size_mm=.8;def.mesh.minimum_size_mm=.1;def.mesh.order=order;
        auto c=pressure();set_face(c,wall);def.conditions={c};
        auto mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
        auto result=app::solve_fem_linear_statics(mesh,def,{210000,.3});
        balance(mesh,result,{},{});
        double radial_moment=0;
        for(std::size_t i=0;i<result.reaction.size();++i){
            const auto p=mesh.surface.vertices[i];radial_moment+=p.x*result.reaction[i].x+p.y*result.reaction[i].y;
        }
        const double exact=c.pressure_MPa*2*std::numbers::pi*25*4;
        check(std::abs(radial_moment/exact-1)<(order==document::FemElementOrder::Linear?.02:.0002),
            "Curved body-face pressure does not follow radial normals");
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=c.id;
        sketch.plane_auto=false;sketch.plane=sketcher::SketchPlane::YZ;sketch.plane_offset=6;
        static_cast<void>(sketch.add_rectangle(1,1,3,3));
        c.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{{std::sqrt(21.),2,2}}};
        def.conditions={c};mesh=app::generate_fem_volume_mesh(solid,def);all_fixed(mesh,def);
        const Vec3 force{-10,-5*(std::sqrt(24.)-4),0};
        balance(mesh,app::solve_fem_linear_statics(mesh,def,{210000,.3}),force,cross({0,0,2},force),.002);
    }
    std::cout<<"Pressure curved cylinder: radial normals, integrated radial moment and bounded projected patch passed\n";
}
}
int main(int argc,char** argv){try{
    if(argc>1){
        // Optional local-data reproduction. Read a native copy; never save it.
        std::vector<kernel::BodyResult> calculated;
        const auto doc=document::PartDocument::load(argv[1],&calculated);
        workspace::Workspace live;live.add_part(doc,std::move(calculated));live.activate(doc.document_id);
        auto& session=live.open_part(doc.document_id)->session;
        const auto material=workspace::material_data(live,doc.document_id);
        double young=std::stod(material.properties.at("YOUNG_MODULUS"));
        const auto unit=material.units.at("YOUNG_MODULUS");
        if(unit=="GPa")young*=1000;else if(unit=="Pa")young*=1e-6;
        else if(unit=="kPa")young*=.001;else if(unit=="psi")young*=.006894757293168;
        else check(unit=="MPa","Unsupported reproduction material unit");
        const double poisson=std::stod(material.properties.at("POISSON_RATIO"));
        for(const auto& body:doc.body_history.bodies())if(body.fem){
            const auto source=workspace::read_fem_source(session,body.scope.id);
            using Clock=std::chrono::steady_clock;
            auto start=Clock::now(),phase_start=start;
            std::optional<app::FemProgressPhase> phase;
            std::size_t iterations=0;
            const auto profile=[&](const app::FemProgress& p) {
                if(phase!=p.phase) {
                    if(phase)std::cout<<"Phase "<<int(*phase)<<": "
                        <<std::chrono::duration<double>(Clock::now()-phase_start).count()<<" s"<<std::endl;
                    phase=p.phase;phase_start=Clock::now();
                }
                if(p.phase==app::FemProgressPhase::SolvingSystem)iterations=p.completed;
            };
            auto mesh=app::generate_fem_volume_mesh(source.get(),*body.fem,profile);
            std::cout<<"Mesh time: "<<std::chrono::duration<double>(Clock::now()-start).count()<<" s"<<std::endl;
            if(argc>4) {
                // Isolate solver timings from the high-order mesher's small
                // run-to-run coordinate changes. Replay a previously verified
                // diagnostic mesh only when connectivity is exactly equal.
                std::ifstream input(argv[4]);nlohmann::json recorded;input>>recorded;
                check(recorded.at("tetrahedra")==nlohmann::json(mesh.tetrahedra)&&
                    recorded.at("order")==int(mesh.order)&&recorded.at("vertices").size()==mesh.surface.vertices.size(),
                    "Diagnostic replay connectivity differs from the generated mesh");
                for(std::size_t i=0;i<mesh.surface.vertices.size();++i) {
                    const auto& p=recorded.at("vertices").at(i);mesh.surface.vertices[i]={p.at(0),p.at(1),p.at(2)};
                }
            }
            std::cout<<"Native FEM: order="<<int(mesh.order)<<" nodes="<<mesh.surface.vertices.size()
                <<" tetrahedra="<<mesh.tetrahedra.size()<<" E="<<young<<" nu="<<poisson<<std::endl;
            if(argc>2){
                nlohmann::json data{{"order",int(mesh.order)},{"tetrahedra",mesh.tetrahedra},{"source_volume",source->volume}};
                data["vertices"]=nlohmann::json::array();
                for(const auto& p:mesh.surface.vertices)data["vertices"].push_back({p.x,p.y,p.z});
                std::ofstream output(argv[2]);output<<data.dump();check(bool(output),"Mesh diagnostic write failed");
            }
            start=Clock::now();
            const auto solution=app::solve_fem_linear_statics(mesh,*body.fem,{young,poisson},profile);
            std::cout<<"Solve time: "<<std::chrono::duration<double>(Clock::now()-start).count()
                <<" s; iterations="<<iterations<<std::endl;
            if(argc>3) {
                start=Clock::now();
                const auto plain=app::solve_fem_linear_statics(mesh,*body.fem,{young,poisson});
                std::cout<<"Unobserved solve time: "<<std::chrono::duration<double>(Clock::now()-start).count()<<" s"<<std::endl;
                check(solution.displacement==plain.displacement&&solution.reaction==plain.reaction&&
                    solution.element_von_mises_MPa==plain.element_von_mises_MPa,"Observed solve differs from unobserved solve");
            }
            Vec3 reaction{};for(const auto r:solution.reaction)reaction=add(reaction,r);
            std::cout<<"Native FEM maximum displacement="<<solution.maximum_displacement_mm<<" mm; reaction="
                <<reaction.x<<','<<reaction.y<<','<<reaction.z<<" N\n";
            if(argc>2) {
                nlohmann::json result{{"stress",solution.element_von_mises_MPa}};
                for(const auto& [name,values]:std::initializer_list<std::pair<const char*,const std::vector<Vec3>*>>{
                        {"displacement",&solution.displacement},{"reaction",&solution.reaction}}) {
                    result[name]=nlohmann::json::array();
                    for(const auto p:*values)result[name].push_back({p.x,p.y,p.z});
                }
                std::ofstream output(std::string(argv[2])+".result.json");output<<result.dump();
                check(bool(output),"Result diagnostic write failed");
            }
        }
    }else{single_element();stress_fields();generated_regions();curved_regions();}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
