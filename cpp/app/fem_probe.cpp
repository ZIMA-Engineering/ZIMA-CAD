#include "fem_probe.hpp"
#include "fem_integration.hpp"

namespace zima::app {
namespace {
using namespace fem_integration;
constexpr std::array<Vector,4> linear{{{-1,-1,-1},{1,0,0},{0,1,0},{0,0,1}}};
constexpr std::array<std::array<unsigned,2>,6> edges{{{0,1},{1,2},{2,0},{0,3},{2,3},{1,3}}};
bool inverse(const std::array<Vector,3>& jacobian,Vector residual,Vector& increment) {
    const std::array<Vector,3> cofactors{cross(jacobian[1],jacobian[2]),
        cross(jacobian[2],jacobian[0]),cross(jacobian[0],jacobian[1])};
    const double determinant=inner(jacobian[0],cofactors[0]);
    if(!std::isfinite(determinant)||std::abs(determinant)<=1e-12*
        length(jacobian[0])*length(jacobian[1])*length(jacobian[2]))return false;
    for(unsigned d=0;d<3;++d)increment[d]=inner(cofactors[d],residual)/determinant;
    return true;
}
}
std::optional<FemProbeLocation> locate_fem_point(const FemVolumeMesh& mesh,kernel::Vec3 position) {
    const auto target=array(position);
    if(!std::ranges::all_of(target,[](double v){return std::isfinite(v);}))return {};
    const bool quadratic=mesh.order==document::FemElementOrder::Quadratic;
    const unsigned count=quadratic?10:4;
    for(std::size_t index=0;index<mesh.tetrahedra.size();++index) {
        const auto& nodes=mesh.tetrahedra[index];const auto origin=array(mesh.surface.vertices.at(nodes[0]));
        std::array<Vector,10> points{};double scale{};
        for(unsigned i=1;i<count;++i){points[i]=subtract(array(mesh.surface.vertices.at(nodes[i])),origin);scale=std::max(scale,length(points[i]));}
        if(!(scale>0))continue;
        const auto relative=subtract(target,origin);
        // A quadratic tetrahedron's Bernstein control hull encloses the mapped
        // cell. Unlike nodal bounds this remains safe on curved boundary faces.
        Vector low{},high{};
        const auto bound=[&](Vector p){for(unsigned d=0;d<3;++d){low[d]=std::min(low[d],p[d]);high[d]=std::max(high[d],p[d]);}};
        for(unsigned i=1;i<4;++i)bound(points[i]);
        if(quadratic)for(unsigned i=0;i<6;++i){Vector control{};for(unsigned d=0;d<3;++d)
            control[d]=2*points[4+i][d]-.5*(points[edges[i][0]][d]+points[edges[i][1]][d]);bound(control);}
        const double tolerance=scale*1e-9;
        bool outside=false;for(unsigned d=0;d<3;++d)outside|=relative[d]<low[d]-tolerance||relative[d]>high[d]+tolerance;
        if(outside)continue;
        Vector local{};
        if(!inverse({points[1],points[2],points[3]},relative,local))continue;
        for(unsigned iteration=0;iteration<40;++iteration) {
            std::array<double,4> barycentric{1-local[0]-local[1]-local[2],local[0],local[1],local[2]};
            std::array<double,10> shape{};std::array<Vector,10> derivatives{};
            for(unsigned i=0;i<4;++i) {
                shape[i]=quadratic?barycentric[i]*(2*barycentric[i]-1):barycentric[i];
                for(unsigned d=0;d<3;++d)derivatives[i][d]=linear[i][d]*(quadratic?4*barycentric[i]-1:1);
            }
            if(quadratic)for(unsigned i=0;i<6;++i) {
                const auto [a,b]=edges[i];shape[4+i]=4*barycentric[a]*barycentric[b];
                for(unsigned d=0;d<3;++d)derivatives[4+i][d]=4*(barycentric[a]*linear[b][d]+barycentric[b]*linear[a][d]);
            }
            Vector mapped{};std::array<Vector,3> jacobian{};
            for(unsigned i=1;i<count;++i)for(unsigned d=0;d<3;++d) {
                mapped[d]+=shape[i]*points[i][d];
                for(unsigned k=0;k<3;++k)jacobian[k][d]+=derivatives[i][k]*points[i][d];
            }
            const auto residual=subtract(relative,mapped);
            if(length(residual)<=tolerance) {
                if(std::ranges::all_of(barycentric,[](double v){return v>=-1e-8&&v<=1+1e-8;}))
                    return FemProbeLocation{index,shape};
                break;
            }
            Vector increment{};if(!inverse(jacobian,residual,increment))break;
            for(unsigned d=0;d<3;++d)local[d]+=increment[d];
            if(!std::ranges::all_of(local,[](double v){return std::isfinite(v)&&std::abs(v)<100;}))break;
        }
    }
    return {};
}
kernel::Vec3 fem_probe_displacement(const FemVolumeMesh& mesh,const FemProbeLocation& location,
        const std::vector<kernel::Vec3>& values) {
    kernel::Vec3 result{};const auto& nodes=mesh.tetrahedra.at(location.element);
    for(unsigned i=0;i<(mesh.order==document::FemElementOrder::Quadratic?10u:4u);++i) {
        const auto value=values.at(nodes[i]);const double weight=location.weights[i];
        result.x+=weight*value.x;result.y+=weight*value.y;result.z+=weight*value.z;
    }
    return result;
}
double fem_probe_scalar(const FemVolumeMesh& mesh,const FemProbeLocation& location,const std::vector<double>& values) {
    double result{};const auto& nodes=mesh.tetrahedra.at(location.element);
    for(unsigned i=0;i<(mesh.order==document::FemElementOrder::Quadratic?10u:4u);++i)
        result+=location.weights[i]*values.at(nodes[i]);
    return result;
}
}
