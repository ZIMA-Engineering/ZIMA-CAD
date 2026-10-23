#pragma once
// Calculation-only OCCT adapter. UI consumes the resulting persisted packet.
#include <zima/kernel/geometry_kernel.hpp>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
namespace zima::kernel {
inline Handle(Geom_BSplineCurve) restore_bspline_geometry(const BSplineGeometry& data) {
    data.validate();
    const auto count=static_cast<int>(data.poles.size());
    TColgp_Array1OfPnt poles(1,count);TColStd_Array1OfReal weights(1,count);
    for(int i=1;i<=count;++i) {
        const auto& p=data.poles[i-1];poles.SetValue(i,gp_Pnt(p.x,p.y,p.z));weights.SetValue(i,data.weights[i-1]);
    }
    std::vector<double> unique;std::vector<int> multiplicities;
    for(double knot:data.knots) {
        if(unique.empty()||unique.back()!=knot){unique.push_back(knot);multiplicities.push_back(1);}
        else ++multiplicities.back();
    }
    TColStd_Array1OfReal knots(1,static_cast<int>(unique.size()));
    TColStd_Array1OfInteger counts(1,static_cast<int>(unique.size()));
    for(int i=1;i<=static_cast<int>(unique.size());++i){knots.SetValue(i,unique[i-1]);counts.SetValue(i,multiplicities[i-1]);}
    return new Geom_BSplineCurve(poles,weights,knots,counts,static_cast<int>(data.degree),false);
}
inline std::optional<BSplineGeometry> capture_bspline_geometry(const Handle(Geom_Curve)& source,double first,double last) {
    if(source.IsNull())return {};
    auto trimmed=new Geom_TrimmedCurve(source,first,last);
    auto exact=GeomConvert::CurveToBSplineCurve(trimmed);
    if (exact->IsPeriodic()) exact->SetNotPeriodic();
    BSplineGeometry data;
    data.degree=static_cast<unsigned>(exact->Degree());
    for (int i=1;i<=exact->NbPoles();++i) {
        const auto p=exact->Pole(i);
        data.poles.push_back({p.X(),p.Y(),p.Z()});data.weights.push_back(exact->Weight(i));
    }
    for (int i=1;i<=exact->NbKnots();++i)
        data.knots.insert(data.knots.end(),exact->Multiplicity(i),exact->Knot(i));
    data.validate();return data;
}
inline std::optional<BSplineGeometry> capture_bspline_geometry(const Handle(Geom_Curve)& source) {
    if(source.IsNull())return {};
    return capture_bspline_geometry(source,source->FirstParameter(),source->LastParameter());
}
inline std::optional<BSplineGeometry> capture_bspline_geometry(const BRepAdaptor_Curve& curve) {
    if (!curve.Is3DCurve()) return {};
    auto source=Handle(Geom_Curve)::DownCast(curve.Curve().Curve()->Copy());
    source->Transform(curve.Trsf());
    return capture_bspline_geometry(source,curve.FirstParameter(),curve.LastParameter());
}
}
