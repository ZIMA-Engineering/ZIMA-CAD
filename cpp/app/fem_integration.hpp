#pragma once
#include "fem_volume_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace zima::app::fem_integration {
using Vector=std::array<double,3>;
inline Vector array(kernel::Vec3 v){return {v.x,v.y,v.z};}
inline Vector subtract(Vector a,Vector b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline Vector cross(Vector a,Vector b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline double inner(Vector a,Vector b){return std::inner_product(a.begin(),a.end(),b.begin(),0.);}
inline double length(Vector v){return std::sqrt(inner(v,v));}
inline double inner(const std::vector<double>& a,const std::vector<double>& b){
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

inline Element integrate(const FemVolumeMesh& mesh,const std::array<std::uint32_t,10>& nodes,bool quadratic){
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

// Integrate N_i dA with its oriented area vector. Pressure needs the vector,
// while an authored total Force uses its length and normalizes by total area.
template<class F> void integrate_surface(const FemVolumeMesh& mesh,
        const std::array<std::uint32_t,6>& e,F sample,bool consistent_linear=false){
    const auto origin=array(mesh.surface.vertices.at(e[0]));
    const auto relative=[&](unsigned i){return subtract(array(mesh.surface.vertices.at(e[i])),origin);};
    if(mesh.order==document::FemElementOrder::Linear){
        const auto area=cross(relative(1),relative(2));
        if(consistent_linear) {
            // Degree-two quadrature for the Robin boundary mass matrix.
            for(unsigned i=0;i<3;++i) {
                std::array<double,6> shape{1./6,1./6,1./6,0,0,0};shape[i]=2./3;
                sample(shape,area,1./6);
            }
        }else sample(std::array<double,6>{1./3,1./3,1./3,0,0,0},area,.5);
        return;
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
            const auto position=relative(i);
            for(unsigned d=0;d<2;++d)for(unsigned k=0;k<3;++k)tangents[d][k]+=derivative[i][d]*position[k];
        }
        sample(shape,cross(tangents[0],tangents[1]),quadrature_weight);
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

}
