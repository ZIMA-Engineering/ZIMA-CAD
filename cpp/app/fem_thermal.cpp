#include "fem_thermal.hpp"
#include "fem_integration.hpp"
#include <charconv>
#include <limits>
#include <map>

namespace zima::app {
namespace {
using namespace fem_integration;
struct Boundary {
    std::array<std::uint32_t,6> nodes;
    std::array<double,36> mass{};
};
std::vector<Boundary> exterior(const FemVolumeMesh& mesh) {
    struct Face { std::array<std::uint32_t,6> nodes;unsigned count{}; };
    std::map<std::array<std::uint32_t,3>,Face> faces;
    constexpr std::array<std::array<unsigned,2>,6> edges{{{0,1},{1,2},{2,0},{0,3},{2,3},{1,3}}};
    for(const auto& cell:mesh.tetrahedra)for(unsigned opposite=0;opposite<4;++opposite) {
        std::array<unsigned,3> corners{};unsigned next=0;
        for(unsigned i=0;i<4;++i)if(i!=opposite)corners[next++]=i;
        std::array<std::uint32_t,6> face{cell[corners[0]],cell[corners[1]],cell[corners[2]]};
        if(mesh.order==document::FemElementOrder::Quadratic)for(unsigned i=0;i<3;++i)
            for(unsigned edge=0;edge<edges.size();++edge) {
                const auto a=corners[i],b=corners[(i+1)%3];
                if((edges[edge][0]==a&&edges[edge][1]==b)||(edges[edge][0]==b&&edges[edge][1]==a))
                    face[3+i]=cell[4+edge];
            }
        std::array key{face[0],face[1],face[2]};std::ranges::sort(key);
        auto& found=faces[key];found.nodes=face;
        if(++found.count>2)throw std::runtime_error("The FEM generator returned an invalid mesh.");
    }
    std::vector<Boundary> result;
    for(const auto& [key,face]:faces)if(face.count==1)result.push_back({face.nodes});
    return result;
}
}

FemThermalMaterial fem_thermal_material(const document::MaterialData& material) {
    const auto property=material.properties.find("THERMAL_CONDUCTIVITY");
    const auto unit=material.units.find("THERMAL_CONDUCTIVITY");
    double value=std::numeric_limits<double>::quiet_NaN();
    if(property!=material.properties.end()&&unit!=material.units.end()) {
        const auto& text=property->second;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())value=std::numeric_limits<double>::quiet_NaN();
        if(unit->second=="W/(m*K)")value*=.001;
        else if(unit->second=="mm*kg/(s^3*C)")value*=.000001;
        else value=std::numeric_limits<double>::quiet_NaN();
    }
    return {value};
}

FemThermalResult solve_fem_steady_thermal(const FemVolumeMesh& mesh,
        const document::FemDefinition& definition,FemThermalMaterial material,const FemProgressCallback& progress) {
    document::validate_fem_definition(definition);
    if(!definition.thermal)throw std::runtime_error("Set the thermal analysis conditions before solving.");
    if(!std::isfinite(material.conductivity_W_mmK)||material.conductivity_W_mmK<=0)
        throw std::runtime_error("Set a positive thermal conductivity in the Part material.");
    if(mesh.tetrahedra.empty())throw std::runtime_error("Generate the FEM mesh before solving.");
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    const unsigned count=quadratic?10:4,samples=quadratic?4:1,face_count=quadratic?6:3;
    const auto size=mesh.surface.vertices.size();
    const double k=material.conductivity_W_mmK,h=definition.thermal->convection_W_m2K*1e-6;
    std::vector<Element> elements;elements.reserve(mesh.tetrahedra.size());
    std::vector<double> load(size),diagonal(size);
    report_fem_progress(progress,FemProgressPhase::AssemblingSystem,0,mesh.tetrahedra.size());
    for(const auto& cell:mesh.tetrahedra) {
        elements.push_back(integrate(mesh,cell,quadratic));
        for(unsigned q=0;q<samples;++q) {
            const auto& p=elements.back().points[q];
            for(unsigned i=0;i<count;++i)diagonal.at(cell[i])+=k*p.weight*inner(p.gradients[i],p.gradients[i]);
        }
        report_fem_progress(progress,FemProgressPhase::AssemblingSystem,elements.size(),mesh.tetrahedra.size());
    }
    auto boundaries=exterior(mesh);
    for(auto& face:boundaries) {
        integrate_surface(mesh,face.nodes,[&](const auto& shape,Vector area,double weight) {
            const double factor=h*length(area)*weight;
            for(unsigned i=0;i<face_count;++i)for(unsigned j=0;j<face_count;++j)
                face.mass[i*6+j]+=factor*shape[i]*shape[j];
        },true);
        for(unsigned i=0;i<face_count;++i)diagonal.at(face.nodes[i])+=face.mass[i*6+i];
    }
    report_fem_progress(progress,FemProgressPhase::ApplyingConditions);
    FemThermalResult result;
    double absolute_power{};
    for(const auto& condition:definition.conditions)if(condition.kind==document::FemConditionKind::HeatSource) {
        if(!condition.placement.reference_valid)throw std::runtime_error("The condition placement is unresolved.");
        const auto region=mesh.condition_regions.find(condition.id);
        if(region==mesh.condition_regions.end()||region->second.elements.empty())
            throw std::runtime_error("The FEM load has no mesh region.");
        if(region->second.kind!=document::FemRegionKind::Surface)
            throw std::runtime_error("A heat source requires a surface region.");
        double area{};
        std::map<std::uint32_t,double> weights;
        for(const auto& face:region->second.elements)
            integrate_surface(mesh,face,[&](const auto& shape,Vector normal,double weight) {
                const double measure=length(normal)*weight;area+=measure;
                for(unsigned i=0;i<face_count;++i)weights[face[i]]+=measure*shape[i];
            });
        if(!std::isfinite(area)||area<=0)throw std::runtime_error("The FEM load has no mesh region.");
        for(const auto& [node,weight]:weights)load.at(node)+=condition.heat_W*weight/area;
        result.input_W+=condition.heat_W;absolute_power+=std::abs(condition.heat_W);
    }
    const auto multiply=[&](const std::vector<double>& input,std::vector<double>& output) {
        std::ranges::fill(output,0.);
        for(const auto& element:elements)for(unsigned q=0;q<samples;++q) {
            const auto& p=element.points[q];Vector gradient{};
            for(unsigned i=0;i<count;++i)for(unsigned d=0;d<3;++d)
                gradient[d]+=input[element.nodes[i]]*p.gradients[i][d];
            for(unsigned i=0;i<count;++i)output[element.nodes[i]]+=k*p.weight*inner(gradient,p.gradients[i]);
        }
        for(const auto& face:boundaries)for(unsigned i=0;i<face_count;++i)for(unsigned j=0;j<face_count;++j)
            output[face.nodes[i]]+=face.mass[i*6+j]*input[face.nodes[j]];
    };
    // Solve temperature rise, avoiding cancellation of ambient terms in K*T.
    std::vector<double> rise(size),residual=load,direction(size),product(size);
    double rho{};
    for(std::size_t i=0;i<size;++i) {
        if(!std::isfinite(diagonal[i])||diagonal[i]<=0)
            throw std::runtime_error("The FEM generator returned an invalid mesh.");
        direction[i]=residual[i]/diagonal[i];rho+=residual[i]*direction[i];
    }
    const double load_norm=inner(load,load),target=std::max(1e-26,load_norm*1e-20);
    const auto failed=[] {throw std::runtime_error("The thermal calculation did not converge.");};
    report_fem_progress(progress,FemProgressPhase::SolvingSystem);
    for(std::size_t iteration=0;inner(residual,residual)>target;++iteration) {
        if(iteration>=std::min<std::size_t>(std::max<std::size_t>(size*2,100),30000))failed();
        report_fem_progress(progress,FemProgressPhase::SolvingSystem,iteration+1);
        multiply(direction,product);
        const double curvature=inner(direction,product);
        if(!std::isfinite(curvature)||curvature<=0)failed();
        const double step=rho/curvature;double next{};
        for(std::size_t i=0;i<size;++i) {
            rise[i]+=step*direction[i];residual[i]-=step*product[i];
            next+=residual[i]*residual[i]/diagonal[i];
        }
        if(!std::isfinite(next))failed();
        for(std::size_t i=0;i<size;++i)direction[i]=residual[i]/diagonal[i]+next/rho*direction[i];
        rho=next;
    }
    report_fem_progress(progress,FemProgressPhase::ComputingTemperature);
    multiply(rise,product);double error{};
    for(std::size_t i=0;i<size;++i) {
        error+=(product[i]-load[i])*(product[i]-load[i]);
        const double temperature=definition.thermal->ambient_C+rise[i];
        if(!std::isfinite(temperature)||temperature< -273.15)
            throw std::runtime_error("The calculated temperature is below absolute zero or is not finite.");
        result.temperature_C.push_back(temperature);
    }
    result.relative_residual=std::sqrt(error/std::max(load_norm,1e-26));
    if(error>std::max(1e-22,load_norm*1e-16))failed();
    for(const auto& face:boundaries)for(unsigned i=0;i<face_count;++i)for(unsigned j=0;j<face_count;++j)
        result.output_W+=face.mass[i*6+j]*rise[face.nodes[j]];
    if(std::abs(result.output_W-result.input_W)>std::max(1e-9,absolute_power*1e-7))
        throw std::runtime_error("The thermal power balance did not converge.");
    const auto [low,high]=std::minmax_element(result.temperature_C.begin(),result.temperature_C.end());
    result.minimum_C=*low;result.maximum_C=*high;
    return result;
}
}
