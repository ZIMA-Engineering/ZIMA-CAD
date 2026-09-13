#pragma once
#include <zima/sketcher/curve_geometry.hpp>
#include <fstream>
#include <iomanip>
#include <cmath>

namespace zima::test {
inline std::vector<kernel::BSplineGeometry> unclamped_dxf_curves() {
    kernel::BSplineGeometry open{2,{{0,0,0},{1,1,0},{2,0,0}},{0,1,2,3,4,5},{1,1,1}};
    auto rational=open;rational.weights={1,2,1};
    kernel::BSplineGeometry periodic{2,{{0,0,0},{4,0,0},{4,4,0},{0,4,0},{0,0,0},{4,0,0}},
        {0,1,2,3,4,5,6,7,8},{1,1,1,1,1,1}};
    auto rational_periodic=periodic;rational_periodic.weights={1,2,3,2,1,2};
    return {open,rational,periodic,rational_periodic};
}
inline void write_control_spline(std::ostream& out,const kernel::BSplineGeometry& c,int flags) {
    out<<std::setprecision(17)<<"0\nSPLINE\n8\nPROFILE\n70\n"<<flags<<"\n71\n"<<c.degree<<"\n72\n"<<c.knots.size()<<"\n73\n"<<c.poles.size()<<"\n74\n0\n";
    for(double k:c.knots)out<<"40\n"<<k<<'\n';
    for(double w:c.weights)out<<"41\n"<<w<<'\n';
    for(const auto& p:c.poles)out<<"10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';
}
inline void write_unclamped_dxf(const std::filesystem::path& path) {
    std::ofstream out(path);out<<"0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    const auto curves=unclamped_dxf_curves();const int flags[]={8,12,11,15};
    for(std::size_t i=0;i<curves.size();++i)write_control_spline(out,curves[i],flags[i]);
    out<<"0\nENDSEC\n0\nEOF\n";
}
inline void check_unclamped_dxf_sketch(const sketcher::Sketch& sketch) {
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto near=[&](double a,double b){require(std::abs(a-b)<1e-9,"Clamped DXF spline changed its exact geometry");};
    require(sketch.bsplines.size()==4&&sketch.import_blocks.size()==1,"DXF lost unclamped/periodic spline entities");
    const auto originals=unclamped_dxf_curves();
    for(std::size_t index=0;index<4;++index) {
        const auto& spline=sketch.bsplines[index];const auto curve=sketcher::sketch_curve_geometry(sketch,spline.id);curve.validate();
        require(spline.closed==(index>=2),"DXF changed spline closure");
        for(int i=0;i<=100;++i) {
            const double t=i/100.,s=1-t;const auto p=kernel::bspline_value(curve,t);
            if(index==0){near(p.x,.5+t);near(p.y,.5+t-t*t);}
            else if(index==1){const double d=1.5*s*s+4*s*t+1.5*t*t;near(p.x,(s*s+4*s*t+2*t*t)/d);near(p.y,(s*s+4*s*t+t*t)/d);}
            else {const auto q=kernel::bspline_value(originals[index],t);near(p.x,q.x);near(p.y,q.y);}
        }
        if(index>=2){near(curve.poles.front().x,curve.poles.back().x);near(curve.poles.front().y,curve.poles.back().y);}
    }
}
}
