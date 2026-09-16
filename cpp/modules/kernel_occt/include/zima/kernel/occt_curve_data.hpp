#pragma once
// Calculation-only OCCT adapter. UI consumes the resulting persisted packet.
#include <zima/kernel/geometry_kernel.hpp>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
namespace zima::kernel {
inline std::optional<BSplineGeometry> capture_bspline_geometry(const BRepAdaptor_Curve& curve) {
    if (!curve.Is3DCurve()) return {};
    auto source=Handle(Geom_Curve)::DownCast(curve.Curve().Curve()->Copy());
    source->Transform(curve.Trsf());
    auto trimmed=new Geom_TrimmedCurve(source,curve.FirstParameter(),curve.LastParameter());
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
}
