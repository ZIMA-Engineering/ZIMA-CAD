#include <zima/kernel/occt_boundary_intersection_graph.hpp>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using zima::kernel::OcctBoundaryCurveKind;
using zima::kernel::OcctBoundaryFace;

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

std::vector<OcctBoundaryFace> faces(const TopoDS_Shape& shape,
        const std::string& owner,std::size_t history_index) {
    std::vector<OcctBoundaryFace> result;
    std::size_t index{};
    for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
        const auto semantic="face:"+std::to_string(index++);
        result.push_back({TopoDS::Face(face.Current()),
            {{owner,semantic,{}},{owner+":alias",semantic,{}}},
            history_index});
    }
    return result;
}

struct CurveInterval {
    occ::handle<Geom_Curve> curve;
    double first{},last{};
    Bnd_Box bounds;
};

std::vector<CurveInterval> section_curves(const TopoDS_Shape& first,
        const TopoDS_Shape& second,double tolerance) {
    BRepAlgoAPI_Section section(first,second,false);
    section.SetNonDestructive(true);section.SetRunParallel(false);
    section.SetFuzzyValue(tolerance);section.Approximation(true);
    section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);
    section.Build();
    require(section.IsDone(),"Reference Section failed");
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
    std::vector<CurveInterval> result;
    for(int index=1;index<=edges.Extent();++index) {
        const auto edge=TopoDS::Edge(edges(index));
        double begin{},end{};
        auto curve=BRep_Tool::Curve(edge,begin,end);
        require(!curve.IsNull()&&end>begin,
            "Reference Section returned an invalid edge");
        CurveInterval interval{curve,begin,end,{}};
        BRepBndLib::Add(edge,interval.bounds,true);
        result.push_back(std::move(interval));
    }
    return result;
}

bool covered_by_union(const CurveInterval& source,
        const std::vector<CurveInterval>& candidates,double tolerance) {
    constexpr int samples=13;
    for(int sample=0;sample<samples;++sample) {
        gp_Pnt point;
        source.curve->D0(source.first+
            (source.last-source.first)*sample/(samples-1.),point);
        bool covered=false;
        for(const auto& candidate:candidates) {
            if(candidate.bounds.IsOut(point))continue;
            GeomAPI_ProjectPointOnCurve projection(point,candidate.curve,
                candidate.first,candidate.last);
            if(projection.NbPoints()&&projection.LowerDistance()<=
                    std::max(1.0e-6,2.*tolerance)) {
                covered=true;break;
            }
        }
        if(!covered)return false;
    }
    return true;
}

void verify_case(const char* name,const TopoDS_Shape& first_shape,
        const TopoDS_Shape& second_shape,bool expect_coplanar) {
    constexpr double tolerance=1.0e-7;
    const auto first=faces(first_shape,"oldest",1);
    const auto second=faces(second_shape,"tool",2);
    const auto graph=zima::kernel::occt_boundary_intersection_graph(
        first,second,tolerance);
    require(!graph.curves.empty(),"Boundary graph found no intersections");
    require(graph.metrics.candidate_pairs<=graph.metrics.possible_pairs,
        "Boundary graph broad phase expanded its candidate set");
    require((graph.metrics.retained_coplanar_intervals>0)==expect_coplanar,
        "Boundary graph classified coplanar intersections incorrectly");
    std::vector<CurveInterval> graph_curves;
    graph_curves.reserve(graph.curves.size());
    for(const auto& curve:graph.curves) {
        require(!curve.curve.IsNull()&&!curve.first_pcurve.IsNull()&&
                !curve.second_pcurve.IsNull()&&
                curve.last_parameter>curve.first_parameter,
            "Boundary graph curve is incomplete");
        require(curve.first_parents.size()==2&&
                curve.second_parents.size()==2&&
                curve.first_parents.front().owner_id=="oldest"&&
                curve.second_parents.front().owner_id=="tool"&&
                curve.first_history_index==1&&
                curve.second_history_index==2,
            "Boundary graph changed stable parent age or aliases");
        CurveInterval interval{curve.curve,curve.first_parameter,
            curve.last_parameter,{}};
        GeomAdaptor_Curve adaptor(curve.curve,curve.first_parameter,
            curve.last_parameter);
        BndLib_Add3dCurve::Add(adaptor,
            std::max(1.0e-6,2.*tolerance),interval.bounds);
        graph_curves.push_back(std::move(interval));
    }
    const auto reference=section_curves(first_shape,second_shape,tolerance);
    require(!reference.empty(),"Reference Section found no intersections");
    const auto covered_reference=std::ranges::count_if(reference,[&](const auto& curve) {
        return covered_by_union(curve,graph_curves,tolerance);
    });
    const auto covered_graph=std::ranges::count_if(graph_curves,[&](const auto& curve) {
        return covered_by_union(curve,reference,tolerance);
    });
    if(covered_reference!=static_cast<std::ptrdiff_t>(reference.size())||
       covered_graph!=static_cast<std::ptrdiff_t>(graph_curves.size())) {
        std::cerr<<name<<" graph_curves="<<graph_curves.size()
            <<" reference_curves="<<reference.size()
            <<" covered_reference="<<covered_reference
            <<" covered_graph="<<covered_graph
            <<" transverse="<<graph.metrics.retained_transverse_intervals
            <<" coplanar="<<graph.metrics.retained_coplanar_intervals<<'\n';
        const auto print=[](const char* label,
                const std::vector<CurveInterval>& curves) {
            for(std::size_t index=0;index<curves.size();++index) {
                gp_Pnt first,middle,last;
                curves[index].curve->D0(curves[index].first,first);
                curves[index].curve->D0(
                    (curves[index].first+curves[index].last)/2.,middle);
                curves[index].curve->D0(curves[index].last,last);
                std::cerr<<label<<'['<<index<<"] p="
                    <<curves[index].first<<','<<curves[index].last
                    <<" xyz="<<first.X()<<','<<first.Y()<<','<<first.Z()
                    <<" / "<<middle.X()<<','<<middle.Y()<<','<<middle.Z()
                    <<" / "<<last.X()<<','<<last.Y()<<','<<last.Z()<<'\n';
            }
        };
        print("graph",graph_curves);print("section",reference);
    }
    require(covered_reference==static_cast<std::ptrdiff_t>(reference.size()),
        "Boundary graph does not cover the reference Section");
    require(covered_graph==static_cast<std::ptrdiff_t>(graph_curves.size()),
        "Boundary graph invented geometry outside the reference Section");
}

void verify_tangent_plane_cylinder() {
    constexpr double tolerance=1.0e-7;
    const auto cylinder_shape=BRepPrimAPI_MakeCylinder(10.,20.).Shape();
    const auto box_shape=BRepPrimAPI_MakeBox(
        gp_Pnt(10.,-5.,3.),5.,10.,10.).Shape();
    TopoDS_Face cylinder_face,plane_face;
    for(TopExp_Explorer face(cylinder_shape,TopAbs_FACE);face.More();
            face.Next()) {
        const auto candidate=TopoDS::Face(face.Current());
        if(BRepAdaptor_Surface(candidate,true).GetType()==GeomAbs_Cylinder)
            cylinder_face=candidate;
    }
    for(TopExp_Explorer face(box_shape,TopAbs_FACE);face.More();
            face.Next()) {
        const auto candidate=TopoDS::Face(face.Current());
        const BRepAdaptor_Surface surface(candidate,true);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();
        if(std::abs(plane.Location().X()-10.)<=tolerance&&
           std::abs(std::abs(plane.Axis().Direction().X())-1.)<=1.0e-12)
            plane_face=candidate;
    }
    require(!cylinder_face.IsNull()&&!plane_face.IsNull(),
        "Tangent plane/cylinder fixture is incomplete");
    const std::vector<OcctBoundaryFace> first{{plane_face,
        {{"plane","face",{}}},1}};
    const std::vector<OcctBoundaryFace> second{{cylinder_face,
        {{"cylinder","face",{}}},2}};
    const auto graph=zima::kernel::occt_boundary_intersection_graph(
        first,second,tolerance);
    require(graph.curves.size()==1,
        "Tangent plane/cylinder contact omitted its exact line");
    const auto& curve=graph.curves.front();
    require(curve.kind==OcctBoundaryCurveKind::Transverse&&
            !curve.first_pcurve.IsNull()&&!curve.second_pcurve.IsNull(),
        "Tangent plane/cylinder contact has incomplete support data");
    gp_Pnt start,end;
    curve.curve->D0(curve.first_parameter,start);
    curve.curve->D0(curve.last_parameter,end);
    require(std::abs(start.X()-10.)<=1.0e-8&&
            std::abs(end.X()-10.)<=1.0e-8&&
            std::abs(start.Y())<=1.0e-8&&std::abs(end.Y())<=1.0e-8&&
            std::abs(std::min(start.Z(),end.Z())-3.)<=1.0e-8&&
            std::abs(std::max(start.Z(),end.Z())-13.)<=1.0e-8,
        "Tangent plane/cylinder contact changed its trimmed interval");
}

} // namespace

int main() try {
    verify_tangent_plane_cylinder();
    verify_case("crossing boxes",
        BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(4,3,-2),8,9,14).Shape(),false);
    verify_case("coplanar boxes",
        BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(4,0,0),8,10,10).Shape(),true);
    verify_case("cylinder and box",
        BRepPrimAPI_MakeCylinder(4,14).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-5,-5,2),10,10,6).Shape(),false);
    std::cout<<"OCCT boundary intersection graph: transverse, coplanar, curved, stable-parent and Section-equivalence checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
