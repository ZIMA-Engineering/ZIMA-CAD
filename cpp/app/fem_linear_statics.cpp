#include "fem_linear_statics.hpp"
#include <zima/document/fem_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace zima::app {
namespace {
using Vector=std::array<double,3>;
Vector array(kernel::Vec3 v){return {v.x,v.y,v.z};}
Vector subtract(Vector a,Vector b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector cross(Vector a,Vector b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double inner(Vector a,Vector b){return std::inner_product(a.begin(),a.end(),b.begin(),0.);}
double length(Vector v){return std::sqrt(inner(v,v));}
double inner(const std::vector<double>& a,const std::vector<double>& b){
    return std::inner_product(a.begin(),a.end(),b.begin(),0.);
}

struct IntegrationPoint {
    std::array<Vector,10> gradients;
    double weight{};
};
struct Element {
    std::array<std::uint32_t,10> nodes;
    std::array<IntegrationPoint,4> points;
};

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

Element integrate(const FemVolumeMesh& mesh,const std::array<std::uint32_t,10>& nodes,bool quadratic){
    static constexpr std::array<Vector,4> linear_gradients{{{-1,-1,-1},{1,0,0},{0,1,0},{0,0,1}}};
    // Gmsh tetrahedron edge nodes in the same order as the mesher output.
    static constexpr std::array<std::array<unsigned,2>,6> edges{{{0,1},{1,2},{2,0},{0,3},{2,3},{1,3}}};
    constexpr double a=.5854101966249685,b=.1381966011250105;
    Element element{nodes,{}};
    const unsigned count=quadratic?10:4;
    const auto origin=array(mesh.surface.vertices.at(nodes[0]));
    double orientation{};
    for(unsigned q=0;q<(quadratic?4u:1u);++q){
        auto& point=element.points[q];
        std::copy(linear_gradients.begin(),linear_gradients.end(),point.gradients.begin());
        if(quadratic){
            std::array<double,4> barycentric{b,b,b,b};barycentric[q]=a;
            for(unsigned i=0;i<4;++i)for(unsigned d=0;d<3;++d)
                point.gradients[i][d]=(4*barycentric[i]-1)*linear_gradients[i][d];
            for(unsigned j=0;j<edges.size();++j){
                const auto [i,k]=edges[j];
                for(unsigned d=0;d<3;++d)point.gradients[4+j][d]=4*(
                    barycentric[i]*linear_gradients[k][d]+barycentric[k]*linear_gradients[i][d]);
            }
        }
        std::array<Vector,3> jacobian{};
        for(unsigned i=1;i<count;++i){
            const auto position=subtract(array(mesh.surface.vertices.at(nodes[i])),origin);
            for(unsigned d=0;d<3;++d)for(unsigned k=0;k<3;++k)
                jacobian[d][k]+=point.gradients[i][d]*position[k];
        }
        const std::array<Vector,3> cofactors{cross(jacobian[1],jacobian[2]),
            cross(jacobian[2],jacobian[0]),cross(jacobian[0],jacobian[1])};
        const double determinant=inner(jacobian[0],cofactors[0]);
        const double scale=length(jacobian[0])*length(jacobian[1])*length(jacobian[2]);
        if(!std::isfinite(determinant)||std::abs(determinant)<=scale*1e-12||determinant*orientation<0)
            throw std::runtime_error("The FEM mesh contains an invalid tetrahedron.");
        orientation=determinant;
        for(unsigned i=0;i<count;++i){
            const auto local=point.gradients[i];
            for(unsigned d=0;d<3;++d)point.gradients[i][d]=
                (local[0]*cofactors[0][d]+local[1]*cofactors[1][d]+local[2]*cofactors[2][d])/determinant;
        }
        point.weight=std::abs(determinant)/(quadratic?24.:6.);
    }
    return element;
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
        if(!quadratic){
            const double area=length(cross(edge,subtract(pos(e[2]),pos(e[0]))))/2;
            measure+=area;
            for(unsigned i=0;i<3;++i)weights.at(e[i])+=area/3;
            continue;
        }
        static constexpr std::array<std::array<unsigned,2>,3> edges{{{0,1},{1,2},{2,0}}};
        static constexpr std::array<std::array<double,2>,3> gradients{{{-1,-1},{1,0},{0,1}}};
        const auto integrate_triangle=[&](Vector barycentric,double quadrature_weight){
            std::array<double,6> shape{};
            std::array<std::array<double,2>,6> derivative{};
            for(unsigned i=0;i<3;++i){
                shape[i]=barycentric[i]*(2*barycentric[i]-1);
                for(unsigned d=0;d<2;++d)derivative[i][d]=(4*barycentric[i]-1)*gradients[i][d];
                const auto [j,k]=edges[i];
                shape[3+i]=4*barycentric[j]*barycentric[k];
                for(unsigned d=0;d<2;++d)derivative[3+i][d]=4*(
                    barycentric[j]*gradients[k][d]+barycentric[k]*gradients[j][d]);
            }
            std::array<Vector,2> tangents{};
            for(unsigned i=1;i<6;++i){
                const auto relative=subtract(pos(e[i]),pos(e[0]));
                for(unsigned d=0;d<2;++d)for(unsigned k=0;k<3;++k)tangents[d][k]+=derivative[i][d]*relative[k];
            }
            const double w=length(cross(tangents[0],tangents[1]))*quadrature_weight;
            measure+=w;
            for(unsigned i=0;i<6;++i)weights.at(e[i])+=w*shape[i];
        };
        integrate_triangle({1./3,1./3,1./3},.1125);
        for(unsigned i=0;i<3;++i){
            Vector first{.470142064105115,.470142064105115,.470142064105115};
            Vector second{.101286507323456,.101286507323456,.101286507323456};
            first[i]=.059715871789770;second[i]=.797426985353087;
            integrate_triangle(first,.066197076394253);
            integrate_triangle(second,.062969590272414);
        }
    }
    if(!std::isfinite(measure)||measure<=0)throw std::runtime_error("The FEM load has no mesh region.");
    const auto direction=array(document::fem_force_direction(condition));
    for(std::size_t i=0;i<weights.size();++i)for(unsigned d=0;d<3;++d)
        force[3*i+d]+=condition.force_N*(weights[i]/measure)*direction[d];
}
}

FemStaticResult solve_fem_linear_statics(const FemVolumeMesh& mesh,
        const document::FemDefinition& definition,FemElasticMaterial material){
    if(!std::isfinite(material.young_MPa)||material.young_MPa<=0||
        !std::isfinite(material.poisson)||material.poisson<=-1||material.poisson>=.5)
        throw std::runtime_error("Set a valid Young's modulus and Poisson ratio in the Part material.");
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
    for(const auto& cell:mesh.tetrahedra){
        elements.push_back(integrate(mesh,cell,quadratic));
        for(unsigned q=0;q<samples;++q){
            const auto& point=elements.back().points[q];
            for(unsigned i=0;i<count;++i){
                const auto& grad=point.gradients[i];
                for(unsigned d=0;d<3;++d)diagonal[3*cell[i]+d]+=point.weight*(
                    shear*inner(grad,grad)+(lambda+shear)*grad[d]*grad[d]);
            }
        }
    }
    for(const auto& condition:definition.conditions){
        if(condition.kind==document::FemConditionKind::Pressure)
            throw std::runtime_error("Linear statics currently supports Force and Constraint conditions.");
        const auto found=mesh.condition_regions.find(condition.id);
        if(found==mesh.condition_regions.end())throw std::runtime_error("The FEM load has no mesh region.");
        if(condition.kind==document::FemConditionKind::Force)add_force(mesh,found->second,condition,load);
        else for(const auto node:found->second.nodes)for(unsigned d=0;d<3;++d)
            if(condition.restrained[d])fixed.at(3*node+d)=true;
    }
    check_restraints(mesh,fixed,count);
    const auto stiffness=[&](const std::vector<double>& input,std::vector<double>& output){
        std::ranges::fill(output,0);
        for(const auto& element:elements)for(unsigned q=0;q<samples;++q){
            const auto& point=element.points[q];
            std::array<Vector,3> gradient{};
            for(unsigned i=0;i<count;++i)for(unsigned d=0;d<3;++d)for(unsigned k=0;k<3;++k)
                gradient[d][k]+=input[3*element.nodes[i]+d]*point.gradients[i][k];
            const double volumetric=lambda*(gradient[0][0]+gradient[1][1]+gradient[2][2]);
            for(unsigned i=0;i<count;++i)for(unsigned d=0;d<3;++d){
                double traction=volumetric*point.gradients[i][d];
                for(unsigned k=0;k<3;++k)traction+=shear*(gradient[d][k]+gradient[k][d])*point.gradients[i][k];
                output[3*element.nodes[i]+d]+=point.weight*traction;
            }
        }
    };
    std::vector<double> displacement(size),residual=load,direction(size),product(size);
    double rho=0;
    for(std::size_t i=0;i<size;++i){
        if(fixed[i]||diagonal[i]<=0){residual[i]=0;continue;}
        direction[i]=residual[i]/diagonal[i];rho+=residual[i]*direction[i];
    }
    const double target=std::max(1e-24,inner(residual,residual)*1e-16);
    const auto singular=[] {throw std::runtime_error("The FEM constraints leave rigid-body motion or the calculation did not converge.");};
    for(std::size_t iteration=0;inner(residual,residual)>target;++iteration){
        if(iteration>=std::min<std::size_t>(size,20000))singular();
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
    stiffness(displacement,product);
    FemStaticResult result;
    for(std::size_t i=0;i<size;i+=3){
        result.displacement.push_back({displacement[i],displacement[i+1],displacement[i+2]});
        const auto reaction=[&](auto k){return fixed[k]?product[k]-load[k]:0.;};
        result.reaction.push_back({reaction(i),reaction(i+1),reaction(i+2)});
        result.maximum_displacement_mm=std::max(result.maximum_displacement_mm,
            length({displacement[i],displacement[i+1],displacement[i+2]}));
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
