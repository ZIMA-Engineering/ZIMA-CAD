#include "../app/fem_thermal.hpp"
#include "../app/fem_probe.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance,const char* message){if(std::abs(a-b)>tolerance){std::cerr<<a<<" != "<<b<<'\n';throw std::runtime_error(message);}}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid thermal input accepted");}
app::FemVolumeMesh tetra(bool quadratic) {
    app::FemVolumeMesh mesh;mesh.order=quadratic?document::FemElementOrder::Quadratic:document::FemElementOrder::Linear;
    mesh.surface.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1},{.5,0,0},{.5,.5,0},{0,.5,0},{0,0,.5},{0,.5,.5},{.5,0,.5}};
    if(!quadratic)mesh.surface.vertices.resize(4);
    mesh.tetrahedra.push_back({0,1,2,3,4,5,6,7,8,9});return mesh;
}
document::FemDefinition definition() {
    document::FemDefinition def;def.source_id="solid";def.analysis=document::FemAnalysisKind::SteadyStateThermal;
    def.thermal=document::FemThermalSettings{20,2000};return def;
}
document::FemCondition source(const std::string& id,double power) {
    document::FemCondition c;c.id=id;c.name=id;c.kind=document::FemConditionKind::HeatSource;
    c.region=document::FemRegionKind::Surface;c.heat_W=power;c.references={{"solid","face",{}}};return c;
}
void uniform() {
    const std::array<std::array<std::uint32_t,6>,4> faces{{{0,1,2,4,5,6},{0,1,3,4,9,7},{0,2,3,6,8,7},{1,2,3,5,8,9}}};
    for(bool quadratic:{false,true})for(bool reverse:{false,true})for(double rise:{-10.,0.,20.}) {
        auto mesh=tetra(quadratic);auto def=definition();
        for(unsigned i=0;i<faces.size();++i) {
            const double area=i==3?std::sqrt(3.)/2:.5;
            auto c=source(std::to_string(i),area*.002*rise);def.conditions.push_back(c);
            auto face=faces[i];if(reverse){std::swap(face[1],face[2]);std::swap(face[3],face[5]);}
            mesh.condition_regions[c.id]={document::FemRegionKind::Surface,{}, {face}};
        }
        unsigned progress{};const auto result=app::solve_fem_steady_thermal(mesh,def,{.05},[&](const auto&){++progress;});
        for(double value:result.temperature_C)near(value,20+rise,1e-7,"Uniform Robin temperature failed");
        near(result.input_W,result.output_W,1e-10,"Thermal balance failed");check(progress>0,"No thermal progress");
        const auto roundtrip=nlohmann::json(def).get<document::FemDefinition>();check(roundtrip==def,"Thermal definition round trip failed");
        auto c=source("invalid",1);c.region=document::FemRegionKind::Point;rejects([&]{document::validate_fem_condition(c);});
        def.thermal->convection_W_m2K=0;rejects([&]{(void)app::solve_fem_steady_thermal(mesh,def,{.05});});
    }
}
void independent_linear() {
    auto mesh=tetra(false);auto def=definition();def.conditions={source("one",.1)};
    mesh.condition_regions["one"]={document::FemRegionKind::Surface,{},{{0,1,2}}};
    const auto result=app::solve_fem_steady_thermal(mesh,def,{.05});
    // Independent closed-form T4 stiffness and consistent Robin mass matrices.
    double a[4][5]{};const double g[4][3]{{-1,-1,-1},{1,0,0},{0,1,0},{0,0,1}};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned d=0;d<3;++d)a[i][j]+=.05/6*g[i][d]*g[j][d];
    for(unsigned opposite=0;opposite<4;++opposite) {
        const double area=opposite==0?std::sqrt(3.)/2:.5;
        for(unsigned i=0;i<4;++i)if(i!=opposite)for(unsigned j=0;j<4;++j)if(j!=opposite)
            a[i][j]+=.002*area*(i==j?2:1)/12;
    }
    for(unsigned i=0;i<3;++i)a[i][4]=.1/3;
    for(unsigned p=0;p<4;++p) {
        const double divisor=a[p][p];for(unsigned j=p;j<5;++j)a[p][j]/=divisor;
        for(unsigned i=0;i<4;++i)if(i!=p){const double factor=a[i][p];for(unsigned j=p;j<5;++j)a[i][j]-=factor*a[p][j];}
    }
    for(unsigned i=0;i<4;++i)near(result.temperature_C[i],20+a[i][4],1e-8,"Independent T4 solution failed");
    check(result.temperature_C[0]>result.temperature_C[3],"Heat did not conduct away from its source");
    def.conditions[0].heat_W*=2;const auto twice=app::solve_fem_steady_thermal(mesh,def,{.05});
    for(unsigned i=0;i<4;++i)near(twice.temperature_C[i]-20,2*(result.temperature_C[i]-20),1e-8,"Heat source superposition failed");
    def.thermal->ambient_C=43;const auto shifted=app::solve_fem_steady_thermal(mesh,def,{.05});
    for(unsigned i=0;i<4;++i)near(shifted.temperature_C[i]-twice.temperature_C[i],23,1e-8,"Ambient translation failed");
    rejects([&]{(void)app::solve_fem_steady_thermal(mesh,def,{0});});
    def.thermal->ambient_C=-300;rejects([&]{document::validate_fem_definition(def);});
}
void interior_face() {
    auto mesh=tetra(false);mesh.surface.vertices.push_back({0,0,-1});
    mesh.tetrahedra.push_back({0,2,1,4});
    auto def=definition();
    const std::array<std::array<std::uint32_t,6>,6> faces{{{0,1,3},{0,2,3},{1,2,3},
        {0,1,4},{0,2,4},{1,2,4}}};
    for(unsigned i=0;i<faces.size();++i) {
        const double area=i%3==2?std::sqrt(3.)/2:.5;
        auto c=source(std::to_string(i),area*.002*20);def.conditions.push_back(c);
        mesh.condition_regions[c.id]={document::FemRegionKind::Surface,{}, {faces[i]}};
    }
    const auto result=app::solve_fem_steady_thermal(mesh,def,{.05});
    for(double temperature:result.temperature_C)near(temperature,40,1e-8,"Internal face incorrectly exchanges heat with ambient");
}
void probes() {
    for(bool quadratic:{false,true})for(bool reversed:{false,true}) {
        auto mesh=tetra(quadratic);
        if(reversed)for(auto& p:mesh.surface.vertices)std::swap(p.x,p.y);
        for(bool curved:{false,true}) {
            if(curved&&!quadratic)continue;
            if(curved)mesh.surface.vertices[5].z+=.07;
            std::vector<kernel::Vec3> values;std::vector<double> scalars;
            for(const auto& p:mesh.surface.vertices) {
                values.push_back({.1+2*p.x-3*p.y,4*p.y+5*p.z,-2*p.z+p.x});scalars.push_back(20+7*p.x-4*p.z);
            }
            for(const kernel::Vec3 point: {kernel::Vec3{.1,.1,.1},kernel::Vec3{.2,.2,.2},mesh.surface.vertices[0],mesh.surface.vertices[1]}) {
                const auto hit=app::locate_fem_point(mesh,point);check(hit.has_value(),"Measurement point inside volume was lost");
                const auto v=app::fem_probe_displacement(mesh,*hit,values);
                near(v.x,.1+2*point.x-3*point.y,1e-8,"Measurement X failed");
                near(v.y,4*point.y+5*point.z,1e-8,"Measurement Y failed");near(v.z,-2*point.z+point.x,1e-8,"Measurement Z failed");
                near(std::hypot(v.x,v.y,v.z),std::sqrt(std::pow(.1+2*point.x-3*point.y,2)+
                    std::pow(4*point.y+5*point.z,2)+std::pow(-2*point.z+point.x,2)),1e-8,"Measurement distance failed");
                near(app::fem_probe_scalar(mesh,*hit,scalars),20+7*point.x-4*point.z,1e-8,"Measurement temperature failed");
            }
            check(!app::locate_fem_point(mesh,{2,2,2}),"Exterior point was snapped to volume");
        }
        if(quadratic) {
            mesh=tetra(true);std::vector<double> values;
            for(auto p:mesh.surface.vertices)values.push_back(p.x*p.x+2*p.y*p.z);
            const kernel::Vec3 point{.2,.3,.1};const auto hit=app::locate_fem_point(mesh,point);
            near(app::fem_probe_scalar(mesh,*hit,values),.1,1e-10,"Quadratic interpolation was reduced to linear");
        }
    }
    document::FemCondition point;point.id="point";point.name="Point";point.kind=document::FemConditionKind::MeasurementPoint;
    point.placement.x=1.23456789123;check(nlohmann::json(point).get<document::FemCondition>()==point,"Measurement persistence failed");
    point.region=document::FemRegionKind::Surface;rejects([&]{document::validate_fem_condition(point);});
}
void units() {
    document::MaterialData material;material.properties["THERMAL_CONDUCTIVITY"]="50000";material.units["THERMAL_CONDUCTIVITY"]="mm*kg/(s^3*C)";
    near(app::fem_thermal_material(material).conductivity_W_mmK,.05,1e-15,"Native conductivity unit failed");
    material.properties["THERMAL_CONDUCTIVITY"]="50";material.units["THERMAL_CONDUCTIVITY"]="W/(m*K)";
    near(app::fem_thermal_material(material).conductivity_W_mmK,.05,1e-15,"SI conductivity unit failed");
    material.properties["THERMAL_CONDUCTIVITY"]="50junk";check(!std::isfinite(app::fem_thermal_material(material).conductivity_W_mmK),"Invalid conductivity accepted");
}
}
int main(){try{uniform();independent_linear();interior_face();probes();units();std::cout<<"FEM thermal: analytic T4, uniform T4/T10, internal face exclusion, winding, power balance, superposition, units, persistence and point interpolation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
