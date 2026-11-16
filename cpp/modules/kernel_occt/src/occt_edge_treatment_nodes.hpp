#pragma once

#include <algorithm>
#include <cmath>
#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>

namespace zima::kernel::edge_treatment_nodes {

// Both treatment kinds already own a perpendicular end profile. Sweeping it
// around the shared corner makes the additive or subtractive node with the
// same signed tool as its branches. Called only during explicit calculation.
inline TopoDS_Shape revolved_corner(const TopoDS_Shape& tool,
        const gp_Pnt& origin,const gp_Dir& first,const gp_Dir& second,
        double tolerance) {
    const gp_Vec d1(first),d2(second);
    const gp_Vec n_first=d2-d1*d1.Dot(d2);
    const gp_Vec n_second=d1-d2*d1.Dot(d2);
    const gp_Vec axis=n_first.Crossed(n_second);
    if(n_first.Magnitude()<=1.e-9||n_second.Magnitude()<=1.e-9||
            axis.Magnitude()<=1.e-12)return {};
    for(TopExp_Explorer faces(tool,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());
        const BRepAdaptor_Surface surface(face,true);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();
        if(!plane.Axis().Direction().IsParallel(first,1.e-9)||
                std::abs(gp_Vec(origin,plane.Location()).Dot(d1))>
                    std::max(10.*tolerance,1.e-7))continue;
        BRepPrimAPI_MakeRevol revolve(face,gp_Ax1(origin,gp_Dir(axis)),
            n_first.Angle(n_second));
        if(revolve.IsDone())return revolve.Shape();
    }
    return {};
}

} // namespace zima::kernel::edge_treatment_nodes
