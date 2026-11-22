#include "fem_linear_statics.hpp"
#include "fem_integration.hpp"
#include <zima/document/fem_presentation.hpp>
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace zima::app {
namespace {
using namespace fem_integration;

void validate_thermal_strain(const FemVolumeMesh& mesh,const FemThermalStrain* thermal) {
    if(!thermal)return;
    if(!std::isfinite(thermal->material.coefficient_per_K)||!std::isfinite(thermal->material.reference_C)||
            thermal->material.reference_C< -273.15)
        throw std::runtime_error("Set a valid thermal expansion coefficient and reference temperature in the Part material.");
    if(thermal->temperature_C.size()!=mesh.surface.vertices.size()||
            !std::ranges::all_of(thermal->temperature_C,[](double t){return std::isfinite(t)&&t>=-273.15;}))
        throw std::runtime_error("The FEM temperature field is invalid for thermal expansion.");
}

template<class F> void thermal_samples(const FemVolumeMesh& mesh,const std::array<std::uint32_t,10>& nodes,
        const FemThermalStrain& thermal,FemElasticMaterial material,F sample) {
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    constexpr std::array<std::array<unsigned,2>,6> edges{{{0,1},{1,2},{2,0},{0,3},{2,3},{1,3}}};
    double orientation{};
    const auto at=[&](const std::array<double,4>& barycentric,double weight) {
        std::array<double,10> shape{};
        for(unsigned i=0;i<4;++i)shape[i]=quadratic?barycentric[i]*(2*barycentric[i]-1):barycentric[i];
        if(quadratic)for(unsigned i=0;i<6;++i)shape[i+4]=4*barycentric[edges[i][0]]*barycentric[edges[i][1]];
        double delta{};
        for(unsigned i=0;i<(quadratic?10u:4u);++i)
            delta+=shape[i]*(thermal.temperature_C[nodes[i]]-thermal.material.reference_C);
        const double pressure=material.young_MPa/(1-2*material.poisson)*thermal.material.coefficient_per_K*delta;
        if(!std::isfinite(pressure))throw std::runtime_error("The FEM temperature field is invalid for thermal expansion.");
        sample(integration_point(mesh,nodes,quadratic,barycentric,weight,orientation),pressure);
    };
    if(!quadratic){at({.25,.25,.25,.25},1./6);return;}
    // Positive 4x4x4 Gauss-Legendre quadrature under a Duffy transform.
    // The ordinary four-point stiffness rule cannot exactly integrate the
    // cubic temperature*gradient load even on an affine T10. This rule also
    // integrates the polynomial cofactor*temperature load of curved T10s.
    constexpr std::array<double,4> x{.06943184420297371,.33000947820757187,.6699905217924281,.9305681557970262};
    constexpr std::array<double,4> w{.17392742256872693,.32607257743127307,.32607257743127307,.17392742256872693};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k) {
        const double r=x[i],s=(1-r)*x[j],t=(1-r)*(1-x[j])*x[k];
        at({1-r-s-t,r,s,t},w[i]*w[j]*w[k]*(1-r)*(1-r)*(1-x[j]));
    }
}

void check_restraints(const FemVolumeMesh& mesh,const std::vector<bool>& fixed,unsigned count){
    const auto size=mesh.surface.vertices.size();
    std::vector<std::size_t> parent(size);
    std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](std::size_t node){
        while(parent[node]!=node){parent[node]=parent[parent[node]];node=parent[node];}
        return node;
    };
    for(const auto& element:mesh.tetrahedra)for(unsigned i=1;i<count;++i)
        parent[root(element[i])]=root(element[0]);
    struct RigidModes {
        std::array<std::array<double,6>,6> rows{};
        double scale{};
    };
    std::map<std::size_t,RigidModes> modes;
    for(const auto& element:mesh.tetrahedra){
        const auto group=root(element[0]);
        auto& basis=modes[group];
        const auto origin=array(mesh.surface.vertices[group]);
        for(unsigned i=0;i<count;++i)basis.scale=std::max(basis.scale,
            length(subtract(array(mesh.surface.vertices[element[i]]),origin)));
    }
    for(std::size_t node=0;node<size;++node){
        if(!fixed[3*node]&&!fixed[3*node+1]&&!fixed[3*node+2])continue;
        const auto group=root(node);
        const auto it=modes.find(group);
        if(it==modes.end())continue;
        auto& [rows,scale]=it->second;
        const auto p=subtract(array(mesh.surface.vertices[node]),array(mesh.surface.vertices[group]));
        for(unsigned d=0;d<3;++d)if(fixed[3*node+d]){
            Vector axis{};axis[d]=1;
            const auto rotation=cross(p,axis);
            std::array<double,6> row{axis[0],axis[1],axis[2],
                rotation[0]/scale,rotation[1]/scale,rotation[2]/scale};
            for(unsigned i=0;i<6;++i){
                const double pivot=row[i];
                if(std::abs(pivot)<1e-9)continue;
                if(rows[i][i]==0){
                    for(unsigned j=i;j<6;++j)rows[i][j]=row[j]/pivot;
                    break;
                }
                for(unsigned j=i;j<6;++j)row[j]-=pivot*rows[i][j];
            }
        }
    }
    for(const auto& [id,basis]:modes)for(unsigned d=0;d<6;++d)
        if(basis.rows[d][d]==0)throw std::runtime_error("The FEM constraints leave rigid-body motion.");
}

using FaceKey=std::array<std::uint32_t,3>;
FaceKey face_key(const std::array<std::uint32_t,6>& e){
    FaceKey key{e[0],e[1],e[2]};std::ranges::sort(key);return key;
}
struct FaceSupport {unsigned count{};std::uint32_t opposite{};};
using PressureSupports=std::map<FaceKey,FaceSupport>;
PressureSupports pressure_supports(const FemVolumeMesh& mesh,const document::FemDefinition& definition){
    PressureSupports result;
    for(const auto& c:definition.conditions){
        if(c.kind!=document::FemConditionKind::Pressure||
                (c.projected_sketch&&!c.projected_sketch->project_to_body))continue;
        if(const auto region=mesh.condition_regions.find(c.id);region!=mesh.condition_regions.end())
            for(const auto& e:region->second.elements)result.try_emplace(face_key(e));
    }
    // Index only requested faces, once per solve. Gmsh surface winding alone
    // does not identify the material side (notably on opposite caps and holes).
    if(!result.empty())for(const auto& cell:mesh.tetrahedra)for(unsigned opposite=0;opposite<4;++opposite){
        FaceKey key{};unsigned j=0;
        for(unsigned i=0;i<4;++i)if(i!=opposite)key[j++]=cell[i];
        std::ranges::sort(key);
        if(auto found=result.find(key);found!=result.end()){
            ++found->second.count;found->second.opposite=cell[opposite];
        }
    }
    return result;
}

void add_pressure(const FemVolumeMesh& mesh,const FemMeshConditionRegion& region,
        const document::FemCondition& condition,const PressureSupports& supports,std::vector<double>& force){
    if(region.kind!=document::FemRegionKind::Surface||condition.region!=document::FemRegionKind::Surface)
        throw std::runtime_error("Pressure requires a surface region.");
    if(!std::isfinite(condition.pressure_MPa)||condition.pressure_MPa<0)
        throw std::runtime_error("Invalid FEM condition value.");
    if(!condition.placement.reference_valid)throw std::runtime_error("The condition placement is unresolved.");
    if(region.elements.empty())throw std::runtime_error("The FEM load has no mesh region.");
    const bool planar=condition.projected_sketch&&!condition.projected_sketch->project_to_body;
    const auto sketch_normal=planar?array(document::fem_condition_sketch(condition).normal()):Vector{};
    const unsigned count=mesh.order==document::FemElementOrder::Quadratic?6:3;
    for(const auto& e:region.elements){
        const auto origin=array(mesh.surface.vertices.at(e[0]));
        const auto normal=cross(subtract(array(mesh.surface.vertices.at(e[1])),origin),
            subtract(array(mesh.surface.vertices.at(e[2])),origin));
        Vector outward=sketch_normal;
        if(!planar){
            const auto found=supports.find(face_key(e));
            if(found==supports.end()||found->second.count!=1)
                throw std::runtime_error("The FEM pressure region has no unambiguous surface normal.");
            outward=subtract(origin,array(mesh.surface.vertices.at(found->second.opposite)));
        }
        const double side=inner(normal,outward);
        if(!std::isfinite(side)||std::abs(side)<=1e-12*length(normal)*length(outward))
            throw std::runtime_error("The FEM pressure region has no unambiguous surface normal.");
        // MPa = N/mm². No total-area normalization: twice the area receives
        // twice the force. A planar internal patch is integrated exactly once.
        const double pressure=condition.pressure_MPa*(condition.pressure_reversed?1:-1)*(side>0?1:-1);
        integrate_surface(mesh,e,[&](const auto& shape,Vector area,double weight){
            if(!std::isfinite(length(area))||length(area)<=0)
                throw std::runtime_error("The FEM pressure region has no unambiguous surface normal.");
            for(unsigned i=0;i<count;++i)for(unsigned d=0;d<3;++d)
                force.at(3*e[i]+d)+=pressure*shape[i]*area[d]*weight;
        });
    }
}

void add_force(const FemVolumeMesh& mesh,const FemMeshConditionRegion& region,
        const document::FemCondition& condition,std::vector<double>& force){
    std::vector<double> weights(mesh.surface.vertices.size());
    const auto pos=[&](auto i){return array(mesh.surface.vertices.at(i));};
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    double measure=0;
    for(const auto& e:region.elements){
        if(region.kind==document::FemRegionKind::Point){weights.at(e[0])+=1;measure+=1;continue;}
        const auto edge=subtract(pos(e[1]),pos(e[0]));
        if(region.kind==document::FemRegionKind::Curve){
            if(!quadratic){
                const double segment=length(edge);measure+=segment;
                weights.at(e[0])+=segment/2;weights.at(e[1])+=segment/2;
                continue;
            }
            const auto midpoint=subtract(pos(e[2]),pos(e[0]));
            for(double xi:{-std::sqrt(.6),0.,std::sqrt(.6)}){
                const Vector shape{xi*(xi-1)/2,xi*(xi+1)/2,1-xi*xi};
                Vector tangent{};
                for(unsigned d=0;d<3;++d)tangent[d]=(xi+.5)*edge[d]-2*xi*midpoint[d];
                const double w=length(tangent)*(xi==0?8./9:5./9);measure+=w;
                for(unsigned i=0;i<3;++i)weights.at(e[i])+=w*shape[i];
            }
            continue;
        }
        integrate_surface(mesh,e,[&](const auto& shape,Vector area,double weight){
            const double w=length(area)*weight;
            measure+=w;
            for(unsigned i=0;i<(quadratic?6u:3u);++i)weights.at(e[i])+=w*shape[i];
        });
    }
    if(!std::isfinite(measure)||measure<=0)throw std::runtime_error("The FEM load has no mesh region.");
    const auto direction=array(document::fem_force_direction(condition));
    for(std::size_t i=0;i<weights.size();++i)for(unsigned d=0;d<3;++d)
        force[3*i+d]+=condition.force_N*(weights[i]/measure)*direction[d];
}
FemElementStress element_stress(const Element& element,const std::vector<kernel::Vec3>& displacement,
        bool quadratic,FemElasticMaterial material,const FemVolumeMesh* mesh=nullptr,const FemThermalStrain* thermal=nullptr) {
    const double shear=material.young_MPa/(2*(1+material.poisson));
    const double lambda=2*shear*material.poisson/(1-2*material.poisson);
    FemElementStress result;double volume=0;
    const auto accumulate=[&](const IntegrationPoint& point,double thermal_pressure) {
        std::array<Vector,3> gradient{};
        for(unsigned i=0;i<(quadratic?10u:4u);++i) {
            const auto u=array(displacement.at(element.nodes[i]));
            for(unsigned d=0;d<3;++d)for(unsigned k=0;k<3;++k)gradient[d][k]+=u[d]*point.gradients[i][k];
        }
        const double trace=gradient[0][0]+gradient[1][1]+gradient[2][2];
        const std::array<double,6> stress{lambda*trace+2*shear*gradient[0][0]-thermal_pressure,
            lambda*trace+2*shear*gradient[1][1]-thermal_pressure,lambda*trace+2*shear*gradient[2][2]-thermal_pressure,
            shear*(gradient[0][1]+gradient[1][0]),shear*(gradient[1][2]+gradient[2][1]),
            shear*(gradient[2][0]+gradient[0][2])};
        for(unsigned i=0;i<6;++i)result.components_MPa[i]+=point.weight*stress[i];
        volume+=point.weight;
    };
    if(thermal)thermal_samples(*mesh,element.nodes,*thermal,material,accumulate);
    else for(unsigned q=0;q<(quadratic?4u:1u);++q)accumulate(element.points[q],0);
    for(auto& value:result.components_MPa)value/=volume;
    const auto& s=result.components_MPa;
    result.von_mises_MPa=std::sqrt(.5*((s[0]-s[1])*(s[0]-s[1])+(s[1]-s[2])*(s[1]-s[2])+
        (s[2]-s[0])*(s[2]-s[0]))+3*(s[3]*s[3]+s[4]*s[4]+s[5]*s[5]));
    return result;
}
}

FemExpansionMaterial fem_expansion_material(const document::MaterialData& data) {
    const auto number=[&](const char* key) {
        const auto found=data.properties.find(key);double value=std::numeric_limits<double>::quiet_NaN();
        if(found==data.properties.end())return value;
        const auto& text=found->second;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()?value:std::numeric_limits<double>::quiet_NaN();
    };
    const auto unit=[&](const char* key){const auto it=data.units.find(key);return it==data.units.end()?std::string{}:it->second;};
    double alpha=number("THERMAL_EXPANSION_COEFFICIENT"),reference=number("THERM_EXPANSION_REF_TEMPERATURE");
    const auto alpha_unit=unit("THERMAL_EXPANSION_COEFFICIENT"),reference_unit=unit("THERM_EXPANSION_REF_TEMPERATURE");
    if(alpha_unit=="1/F"||alpha_unit=="1/°F")alpha*=1.8;
    else if(alpha_unit!="1/C"&&alpha_unit!="1/°C"&&alpha_unit!="1/K")alpha=std::numeric_limits<double>::quiet_NaN();
    if(reference_unit=="K")reference-=273.15;
    else if(reference_unit=="F"||reference_unit=="°F")reference=(reference-32)/1.8;
    else if(reference_unit!="C"&&reference_unit!="°C")reference=std::numeric_limits<double>::quiet_NaN();
    return {alpha,reference};
}

std::vector<FemElementStress> fem_element_stresses(const FemVolumeMesh& mesh,
        const std::vector<kernel::Vec3>& displacement,FemElasticMaterial material,const FemThermalStrain* thermal) {
    validate_thermal_strain(mesh,thermal);
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    std::vector<FemElementStress> result;result.reserve(mesh.tetrahedra.size());
    for(const auto& cell:mesh.tetrahedra)result.push_back(element_stress(integrate(mesh,cell,quadratic),displacement,quadratic,material,&mesh,thermal));
    return result;
}

FemStaticResult solve_fem_linear_statics(const FemVolumeMesh& mesh,
        const document::FemDefinition& definition,FemElasticMaterial material,
        const FemProgressCallback& progress,const FemThermalStrain* thermal){
    if(!std::isfinite(material.young_MPa)||material.young_MPa<=0||
        !std::isfinite(material.poisson)||material.poisson<=-1||material.poisson>=.5)
        throw std::runtime_error("Set a valid Young's modulus and Poisson ratio in the Part material.");
    if(definition.analysis==document::FemAnalysisKind::ThermalMechanical&&!thermal)
        throw std::runtime_error("The FEM temperature field is invalid for thermal expansion.");
    validate_thermal_strain(mesh,thermal);
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    const unsigned count=quadratic?10:4,samples=quadratic?4:1;
    const auto size=3*mesh.surface.vertices.size();
    if(mesh.tetrahedra.empty())throw std::runtime_error("Generate the FEM mesh before solving.");
    const double shear=material.young_MPa/(2*(1+material.poisson));
    const double lambda=2*shear*material.poisson/(1-2*material.poisson);
    std::vector<double> load(size),diagonal(size);
    std::vector<bool> fixed(size);
    std::vector<Element> elements;
    elements.reserve(mesh.tetrahedra.size());
    report_fem_progress(progress,FemProgressPhase::AssemblingSystem,0,mesh.tetrahedra.size());
    for(const auto& cell:mesh.tetrahedra){
        elements.push_back(integrate(mesh,cell,quadratic));
        if(thermal)thermal_samples(mesh,cell,*thermal,material,[&](const IntegrationPoint& point,double pressure){
            for(unsigned i=0;i<count;++i)for(unsigned d=0;d<3;++d)
                load[3*cell[i]+d]+=point.weight*pressure*point.gradients[i][d];
        });
        for(unsigned q=0;q<samples;++q){
            const auto& point=elements.back().points[q];
            for(unsigned i=0;i<count;++i){
                const auto& grad=point.gradients[i];
                for(unsigned d=0;d<3;++d)diagonal[3*cell[i]+d]+=point.weight*(
                    shear*inner(grad,grad)+(lambda+shear)*grad[d]*grad[d]);
            }
        }
        report_fem_progress(progress,FemProgressPhase::AssemblingSystem,elements.size(),mesh.tetrahedra.size());
    }
    report_fem_progress(progress,FemProgressPhase::ApplyingConditions);
    const bool heat_only=thermal&&definition.analysis!=document::FemAnalysisKind::ThermalMechanical;
    const auto supports=heat_only?PressureSupports{}:pressure_supports(mesh,definition);
    for(const auto& condition:definition.conditions){
        if(heat_only&&condition.kind!=document::FemConditionKind::Constraint)continue;
        if(condition.kind==document::FemConditionKind::HeatSource||
                document::fem_measurement(condition.kind))continue;
        const auto found=mesh.condition_regions.find(condition.id);
        if(found==mesh.condition_regions.end())throw std::runtime_error("The FEM load has no mesh region.");
        if(condition.kind==document::FemConditionKind::Force)add_force(mesh,found->second,condition,load);
        else if(condition.kind==document::FemConditionKind::Pressure)add_pressure(mesh,found->second,condition,supports,load);
        else for(const auto node:found->second.nodes)for(unsigned d=0;d<3;++d)
            if(condition.restrained[d])fixed.at(3*node+d)=true;
    }
    check_restraints(mesh,fixed,count);
    const auto element_products=[&]<unsigned Count,unsigned Samples>(const std::vector<double>& input,std::vector<double>& output){
        for(const auto& element:elements) {
            std::array<Vector,Count> values;
            for(unsigned i=0;i<Count;++i)for(unsigned d=0;d<3;++d)
                values[i][d]=input[3*element.nodes[i]+d];
            for(unsigned q=0;q<Samples;++q){
                const auto& point=element.points[q];
                std::array<Vector,3> gradient{};
                for(unsigned i=0;i<Count;++i)for(unsigned d=0;d<3;++d)for(unsigned k=0;k<3;++k)
                    gradient[d][k]+=values[i][d]*point.gradients[i][k];
                const double volumetric=lambda*(gradient[0][0]+gradient[1][1]+gradient[2][2]);
                std::array<Vector,3> shear_stress;
                for(unsigned d=0;d<3;++d)for(unsigned k=0;k<3;++k)
                    shear_stress[d][k]=shear*(gradient[d][k]+gradient[k][d]);
                for(unsigned i=0;i<Count;++i)for(unsigned d=0;d<3;++d){
                    double traction=volumetric*point.gradients[i][d];
                    for(unsigned k=0;k<3;++k)traction+=shear_stress[d][k]*point.gradients[i][k];
                    output[3*element.nodes[i]+d]+=point.weight*traction;
                }
            }
        }
    };
    const auto stiffness=[&](const std::vector<double>& input,std::vector<double>& output){
        std::ranges::fill(output,0);
        // Specialize only the fixed element cardinalities. Gather each input
        // node once and reuse the same strain tensor for every test function;
        // integration and floating-point accumulation order stay unchanged.
        if(quadratic)element_products.template operator()<10,4>(input,output);
        else element_products.template operator()<4,1>(input,output);
    };
    std::vector<double> displacement(size),residual=load,direction(size),product(size);
    double rho=0;
    for(std::size_t i=0;i<size;++i){
        if(fixed[i]||diagonal[i]<=0){residual[i]=0;continue;}
        direction[i]=residual[i]/diagonal[i];rho+=residual[i]*direction[i];
    }
    const double target=std::max(1e-24,inner(residual,residual)*1e-16);
    const auto singular=[] {throw std::runtime_error("The FEM constraints leave rigid-body motion or the calculation did not converge.");};
    report_fem_progress(progress,FemProgressPhase::SolvingSystem);
    for(std::size_t iteration=0;inner(residual,residual)>target;++iteration){
        if(iteration>=std::min<std::size_t>(size,20000))singular();
        report_fem_progress(progress,FemProgressPhase::SolvingSystem,iteration+1);
        stiffness(direction,product);
        for(std::size_t i=0;i<size;++i)if(fixed[i])product[i]=0;
        const double curvature=inner(direction,product);
        if(!std::isfinite(curvature)||curvature<=0)singular();
        const double step=rho/curvature;
        double next=0;
        for(std::size_t i=0;i<size;++i){
            displacement[i]+=step*direction[i];residual[i]-=step*product[i];
            if(!fixed[i]&&diagonal[i]>0)next+=residual[i]*residual[i]/diagonal[i];
        }
        if(!std::isfinite(next))singular();
        for(std::size_t i=0;i<size;++i)if(!fixed[i]&&diagonal[i]>0)
            direction[i]=residual[i]/diagonal[i]+(next/rho)*direction[i];
        rho=next;
    }
    report_fem_progress(progress,FemProgressPhase::ComputingResults);
    stiffness(displacement,product);
    FemStaticResult result;
    for(std::size_t i=0;i<size;i+=3){
        result.displacement.push_back({displacement[i],displacement[i+1],displacement[i+2]});
        const auto reaction=[&](auto k){return fixed[k]?product[k]-load[k]:0.;};
        result.reaction.push_back({reaction(i),reaction(i+1),reaction(i+2)});
        result.maximum_displacement_mm=std::max(result.maximum_displacement_mm,
            length({displacement[i],displacement[i+1],displacement[i+2]}));
    }
    result.element_von_mises_MPa.reserve(elements.size());
    report_fem_progress(progress,FemProgressPhase::ComputingResults,0,elements.size());
    for(const auto& element:elements) {
        const auto stress=element_stress(element,result.displacement,quadratic,material,&mesh,thermal);
        result.element_von_mises_MPa.push_back(stress.von_mises_MPa);
        result.maximum_von_mises_MPa=std::max(result.maximum_von_mises_MPa,stress.von_mises_MPa);
        report_fem_progress(progress,FemProgressPhase::ComputingResults,result.element_von_mises_MPa.size(),elements.size());
    }
    return result;
}

kernel::ViewerMesh fem_deformed_mesh(const FemVolumeMesh& mesh,const FemStaticResult& result){
    auto view=mesh.surface;
    // Boundary edges are rebuilt from the displaced nodes to keep the wire
    // coincident with the displayed mesh, including quadratic subdivisions.
    view.edges.clear();
    for(std::size_t i=0;i<view.vertices.size();++i){
        auto& vertex=view.vertices[i];const auto& delta=result.displacement.at(i);
        vertex={vertex.x+delta.x,vertex.y+delta.y,vertex.z+delta.z};
    }
    return view;
}
}
