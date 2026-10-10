#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Plane.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <math.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void close(double actual,double expected,const char* message) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>1e-8)
        throw std::runtime_error(message);
}

TopoDS_Face rectangle(int intervals,bool surfaceKnots,bool extendedBound=false) {
    NCollection_Array1<double> knots(1,intervals+1);
    NCollection_Array1<int> multiplicities(1,intervals+1);
    for(int i=1;i<=intervals+1;++i) {
        knots(i)=double(i-1)/intervals;
        multiplicities(i)=(i==1||i==intervals+1)?2:1;
    }
    occ::handle<Geom_Surface> surface=new Geom_Plane(gp_Pln({0,0,1},{0,0,1}));
    if(surfaceKnots) {
        NCollection_Array2<gp_Pnt> poles(1,intervals+1,1,2);
        for(int i=1;i<=intervals+1;++i)for(int j=1;j<=2;++j)
            poles(i,j)=gp_Pnt(knots(i),j-1,1);
        NCollection_Array1<double> vKnots(1,2);vKnots(1)=0;vKnots(2)=1;
        NCollection_Array1<int> vMult(1,2);vMult(1)=vMult(2)=2;
        surface=new Geom_BSplineSurface(poles,knots,vKnots,multiplicities,vMult,1,1);
    }
    // A trim can cross the last surface knot within the face tolerance. The
    // resulting trailing interval still needs its full adaptive allocation.
    const double width=extendedBound?1.0+1e-10:1.0;
    const gp_Pnt2d points[]={{0,0},{width,0},{width,1},{0,1},{0,0}};
    BRepBuilderAPI_MakeWire wire;
    for(int edge=0;edge<4;++edge) {
        occ::handle<Geom2d_Curve> curve=GCE2d_MakeSegment(points[edge],points[edge+1]).Value();
        if(!surfaceKnots&&edge==1) {
            NCollection_Array1<gp_Pnt2d> poles(1,intervals+1);
            for(int i=1;i<=intervals+1;++i)poles(i)=gp_Pnt2d(1,knots(i));
            curve=new Geom2d_BSplineCurve(poles,knots,multiplicities,1);
        }
        auto boundary=BRepBuilderAPI_MakeEdge(curve,surface).Edge();
        BRepLib::BuildCurve3d(boundary);
        wire.Add(boundary);
    }
    const auto face=BRepBuilderAPI_MakeFace(surface,wire.Wire(),true).Face();
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("High-span fixture is invalid");
    return face;
}

void check(int intervals,bool surfaceKnots,bool extendedBound=false) {
    const auto face=rectangle(intervals,surfaceKnots,extendedBound);
    const double width=extendedBound?1.0+1e-10:1.0;
    GProp_GProps area;
    const auto areaError=BRepGProp::SurfaceProperties(face,area,1e-12);
    if(!std::isfinite(areaError))throw std::runtime_error("Area error is not finite");
    close(area.Mass(),width,"Adaptive integration skipped a knot span in the area");
    const auto areaCenter=area.CentreOfMass();
    close(areaCenter.X(),width/2,"Incorrect surface centroid X");
    close(areaCenter.Y(),.5,"Incorrect surface centroid Y");
    close(areaCenter.Z(),1,"Incorrect surface centroid Z");
    const auto areaTensor=area.MatrixOfInertia();
    close(areaTensor.Value(1,1),width/12,"Incorrect surface inertia X");
    close(areaTensor.Value(2,2),width*width*width/12,"Incorrect surface inertia Y");
    close(areaTensor.Value(3,3),width*(1+width*width)/12,"Incorrect surface inertia Z");
    const auto solid=BRepPrimAPI_MakePrism(face,gp_Vec(0,0,2)).Shape();
    if(!BRepCheck_Analyzer(solid).IsValid())throw std::runtime_error("High-span solid is invalid");
    GProp_GProps volume;
    const auto volumeError=BRepGProp::VolumeProperties(solid,volume,1e-12);
    if(!std::isfinite(volumeError))throw std::runtime_error("Volume error is not finite");
    close(volume.Mass(),2*width,"Adaptive integration skipped a knot span in the volume");
    const auto center=volume.CentreOfMass();
    close(center.X(),width/2,"Incorrect solid centroid X");
    close(center.Y(),.5,"Incorrect solid centroid Y");
    close(center.Z(),2,"Incorrect solid centroid Z");
    const auto tensor=volume.MatrixOfInertia();
    close(tensor.Value(1,1),5*width/6,"Incorrect solid inertia X");
    close(tensor.Value(2,2),width*(width*width+4)/6,"Incorrect solid inertia Y");
    close(tensor.Value(3,3),width*(width*width+1)/6,"Incorrect solid inertia Z");
    for(int row=1;row<=3;++row)for(int col=1;col<=3;++col)
        if(row!=col)close(tensor.Value(row,col),0,"Incorrect solid product of inertia");
    std::cout<<"intervals="<<intervals<<" surface_knots="<<surfaceKnots<<" extended_bound="<<extendedBound<<" passed\n";
}
}

int main() {
    try {
        const int cap=32*math::GaussPointsMax()+1;
        for(bool surfaceKnots:{false,true})for(int intervals:{cap-1,cap,cap+19})
            check(intervals,surfaceKnots);
        check(66,true,true);
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
