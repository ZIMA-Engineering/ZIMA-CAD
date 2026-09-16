#pragma once
#include <zima/kernel/curve_evaluation.hpp>
#include <limits>

namespace zima::kernel {
// Solve exact point-on-curve constraints together with Cartesian plane equations.
// Parameters are bounded by the persisted trimmed domains, never display chords.
inline bool solve_curve_constraints(const std::vector<BSplineGeometry>& curves,
    const std::vector<std::pair<Vec3,double>>& planes, Vec3& origin,
    bool keep_free_coordinates = true) {
    const auto initial=origin; const std::size_t n=3+curves.size();
    std::vector<double> parameters;
    for(const auto& c:curves)parameters.push_back(project_bspline(c,origin).parameter);
    const auto residual=[&](const std::vector<double>& x,std::vector<std::vector<double>>* jac) {
        std::vector<double> result;
        const auto row=[&](double r,std::vector<double> j){result.push_back(r);if(jac)jac->push_back(std::move(j));};
        for(const auto& [normal,rhs]:planes) {
            std::vector<double> j(n);j[0]=normal.x;j[1]=normal.y;j[2]=normal.z;
            row(normal.x*x[0]+normal.y*x[1]+normal.z*x[2]-rhs,std::move(j));
        }
        for(std::size_t i=0;i<curves.size();++i) {
            const auto p=bspline_value(curves[i],x[3+i]),d=bspline_derivative(curves[i],x[3+i]);
            const std::array<double,3> a{p.x,p.y,p.z},b{d.x,d.y,d.z};
            for(std::size_t k=0;k<3;++k) {std::vector<double> j(n);j[k]=1;j[3+i]=-b[k];row(x[k]-a[k],std::move(j));}
        }
        return result;
    };
    const auto norm=[](const auto& values){double r=0;for(double v:values)r+=v*v;return r;};
    bool found=false;double best=std::numeric_limits<double>::infinity();Vec3 solution;
    // Alternate seeds are needed when a closed curve starts at a singular
    // tangent (for example a circle center combined with a diametral plane).
    for(unsigned seed=0;seed<9;++seed) {
        std::vector<double> x{initial.x,initial.y,initial.z};
        x.insert(x.end(),parameters.begin(),parameters.end());
        if(seed && !curves.empty())x[3]=(seed-1)/8.0;
        if(!curves.empty()) {
            // Start on the trimmed curve, including its endpoints. Starting
            // outside a remote segment leaves LM pushing a clamped parameter
            // beyond its boundary instead of moving the origin onto it.
            const auto projected=bspline_value(curves.front(),x[3]);
            x[0]=projected.x;x[1]=projected.y;x[2]=projected.z;
        }
        double lambda=1e-5;
        for(unsigned iteration=0;iteration<120;++iteration) {
            std::vector<std::vector<double>> j;const auto r=residual(x,&j);const double error=norm(r);
            if(error<1e-16) {
                const double dist=(x[0]-initial.x)*(x[0]-initial.x)+(x[1]-initial.y)*(x[1]-initial.y)+(x[2]-initial.z)*(x[2]-initial.z);
                if(dist<best) {best=dist;solution={x[0],x[1],x[2]};found=true;}
                break;
            }
            std::vector<std::vector<double>> a(n,std::vector<double>(n+1));
            for(std::size_t k=0;k<r.size();++k)for(std::size_t i=0;i<n;++i) {
                a[i][n]-=j[k][i]*r[k];
                for(std::size_t l=0;l<n;++l)a[i][l]+=j[k][i]*j[k][l];
            }
            for(std::size_t i=0;i<n;++i)a[i][i]+=lambda;
            bool singular=false;
            for(std::size_t col=0;col<n;++col) {
                auto pivot=col;for(auto k=col+1;k<n;++k)if(std::abs(a[k][col])>std::abs(a[pivot][col]))pivot=k;
                if(std::abs(a[pivot][col])<1e-20){singular=true;break;}
                std::swap(a[col],a[pivot]);const double divisor=a[col][col];
                for(auto k=col;k<=n;++k)a[col][k]/=divisor;
                for(std::size_t i=0;i<n;++i)if(i!=col){const double factor=a[i][col];for(auto k=col;k<=n;++k)a[i][k]-=factor*a[col][k];}
            }
            if(singular)break;
            auto next=x;for(std::size_t i=0;i<n;++i)next[i]+=a[i][n];
            for(std::size_t i=3;i<n;++i)next[i]=std::clamp(next[i],0.,1.);
            if(norm(residual(next,nullptr))<error) {x=std::move(next);lambda=std::max(1e-12,lambda*.25);}
            else {lambda*=10;if(lambda>1e12)break;}
        }
        // A regular local solution retains the current branch. Explore other
        // seeds only when that solve failed, not on every hover or regeneration.
        if(found && seed==0)break;
    }
    if(found && keep_free_coordinates) {
        std::vector<Vec3> rows;for(const auto& [normal,rhs]:planes)rows.push_back(normal);
        for(const auto& curve:curves) {
            const auto d=project_bspline(curve,solution).tangent;
            const double m=std::hypot(d.x,d.y,d.z);if(m<1e-12)return false;
            const Vec3 t{d.x/m,d.y/m,d.z/m},seed=std::abs(t.x)<.8?Vec3{1,0,0}:Vec3{0,1,0};
            const Vec3 a{t.y*seed.z-t.z*seed.y,t.z*seed.x-t.x*seed.z,t.x*seed.y-t.y*seed.x};
            rows.push_back(a);rows.push_back({t.y*a.z-t.z*a.y,t.z*a.x-t.x*a.z,t.x*a.y-t.y*a.x});
        }
        std::vector<std::array<double,3>> matrix;for(const auto& r:rows)matrix.push_back({r.x,r.y,r.z});
        std::array<bool,3> pivot{};std::size_t rank=0;
        for(unsigned col=0;col<3 && rank<matrix.size();++col) {
            auto best=rank;for(auto k=rank+1;k<matrix.size();++k)if(std::abs(matrix[k][col])>std::abs(matrix[best][col]))best=k;
            if(std::abs(matrix[best][col])<1e-8)continue;
            std::swap(matrix[rank],matrix[best]);const double factor=matrix[rank][col];
            for(unsigned j=col;j<3;++j)matrix[rank][j]/=factor;
            for(auto k=rank+1;k<matrix.size();++k){const double f=matrix[k][col];for(unsigned j=col;j<3;++j)matrix[k][j]-=f*matrix[rank][j];}
            pivot[col]=true;++rank;
        }
        auto constrained=planes;const std::array<double,3> values{initial.x,initial.y,initial.z};
        for(unsigned i=0;i<3;++i)if(!pivot[i]) {
            const Vec3 axis{i==0?1.:0.,i==1?1.:0.,i==2?1.:0.};constrained.push_back({axis,values[i]});
        }
        if(constrained.size()!=planes.size()) {
            auto coordinated=solution;
            if(solve_curve_constraints(curves,constrained,coordinated,false))solution=coordinated;
            // Initial attachment can start outside a finite curve's range.
            // Its nearest valid location remains usable when a Cartesian
            // fallback cannot be retained. Explicit edits validate separately.
        }
    }
    if(found)origin=solution;
    return found;
}
} // namespace zima::kernel
