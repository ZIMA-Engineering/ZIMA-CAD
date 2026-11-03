#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_BSplineCurve.hxx>
#include <gp_Circ.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    return std::abs(properties.Mass());
}

double area(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape,properties);
    return std::abs(properties.Mass());
}

void near(double actual,double expected,const char* message) {
    if(std::abs(actual-expected)>
            1.0e-7*std::max({1.,std::abs(actual),std::abs(expected)}))
        throw std::runtime_error(std::string(message)+": "+
            std::to_string(actual)+" != "+std::to_string(expected));
}

bool shares_endpoint(const TopoDS_Edge& first,const TopoDS_Edge& second) {
    TopoDS_Vertex first_a,first_b,second_a,second_b;
    TopExp::Vertices(first,first_a,first_b);
    TopExp::Vertices(second,second_a,second_b);
    return first_a.IsSame(second_a)||first_a.IsSame(second_b)||
        first_b.IsSame(second_a)||first_b.IsSame(second_b);
}

std::vector<TopoDS_Edge> vertical_edges(const TopoDS_Shape& shape,
        double height) {
    std::vector<TopoDS_Edge> result;
    for(TopExp_Explorer item(shape,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        if(curve.GetType()!=GeomAbs_Line)continue;
        TopoDS_Vertex first_vertex,last_vertex;
        TopExp::Vertices(edge,first_vertex,last_vertex);
        if(first_vertex.IsNull()||last_vertex.IsNull())continue;
        const auto first=BRep_Tool::Pnt(first_vertex);
        const auto last=BRep_Tool::Pnt(last_vertex);
        if(std::abs(first.X()-last.X())<1.0e-9&&
           std::abs(first.Y()-last.Y())<1.0e-9&&
           std::abs(std::abs(first.Z()-last.Z())-height)<1.0e-9)
            if(std::ranges::none_of(result,[&](const auto& current) {
                    return current.IsSame(edge);
                }))result.push_back(edge);
    }
    return result;
}

TopoDS_Shape reference_fillet(const TopoDS_Shape& input,
        const std::vector<TopoDS_Edge>& edges,double radius) {
    BRepFilletAPI_MakeFillet fillet(input);
    for(const auto& edge:edges)fillet.Add(radius,edge);
    fillet.Build();
    require(fillet.IsDone()&&!fillet.Shape().IsNull(),
        "OCCT reference Fillet failed");
    require(BRepCheck_Analyzer(fillet.Shape()).IsValid(),
        "OCCT reference Fillet is invalid");
    return fillet.Shape();
}

void verify_result(const TopoDS_Shape& input,
        const std::vector<TopoDS_Edge>& edges,double radius,
        double tolerance=1.0e-7) {
    const auto custom=zima::kernel::occt_planar_edge_fillet(
        input,edges,radius,tolerance);
    const auto reference=reference_fillet(input,edges,radius);
    require(!custom.shape.IsNull()&&custom.history&&
            custom.removal_domains.size()==edges.size()&&
            custom.analytic_removal_domains.size()==edges.size()&&
            std::ranges::all_of(custom.removal_domains,
                [](const auto& shape) {
                    return !shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();
                })&&
            custom.fillet_faces.size()==edges.size()&&
            std::ranges::all_of(custom.fillet_faces,
                [](const auto& faces){return !faces.empty();}),
        "Custom planar Fillet omitted shape, removal domain, history or parent faces");
    const auto distance=[](const auto& first,const auto& second) {
        return std::hypot(first.x-second.x,first.y-second.y,
            first.z-second.z);
    };
    const auto dot=[](const auto& first,const auto& second) {
        return first.x*second.x+first.y*second.y+first.z*second.z;
    };
    for(const auto& domain:custom.analytic_removal_domains) {
        const zima::kernel::Vec3 source_axis{
            domain.source_end.x-domain.source_start.x,
            domain.source_end.y-domain.source_start.y,
            domain.source_end.z-domain.source_start.z};
        require(std::abs(std::hypot(domain.axis_direction.x,
                    domain.axis_direction.y,domain.axis_direction.z)-1.)<
                    1.0e-10&&
                dot(source_axis,domain.axis_direction)>0.&&
                std::abs(distance(domain.center_at_start,
                    domain.first_tangent_at_start)-domain.radius)<1.0e-7&&
                std::abs(distance(domain.center_at_start,
                    domain.second_tangent_at_start)-domain.radius)<1.0e-7&&
                domain.axial_extension==0.,
            "Custom Fillet analytic removal domain is incomplete");
    }
    require(custom.metrics.requested_edges==edges.size()&&
            custom.metrics.cutter_solids==edges.size()&&
            custom.metrics.generated_fillet_faces>=edges.size(),
        "Custom planar Fillet metrics are incomplete");
    for(const auto& domain:custom.removal_domains) {
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(domain,TopAbs_FACE,faces);
        require(faces.Extent()==5,
            "Custom Fillet profile contains auxiliary closure faces");
    }
    for(const auto& faces:custom.fillet_faces)
        for(const auto& face:faces) {
            const BRepAdaptor_Surface support(face,true);
            require(support.GetType()==GeomAbs_Cylinder&&
                    std::abs(support.Cylinder().Radius()-radius)<1.0e-7,
                "Custom Fillet face is not the requested exact cylinder");
            TopTools_IndexedMapOfShape boundary_edges;
            TopExp::MapShapes(face,TopAbs_EDGE,boundary_edges);
            require(boundary_edges.Extent()==4,
                "Custom Fillet face is not bounded by four exact edges");
            std::size_t line_count{},circle_count{};
            for(int index=1;index<=boundary_edges.Extent();++index) {
                switch(BRepAdaptor_Curve(TopoDS::Edge(
                        boundary_edges.FindKey(index))).GetType()) {
                case GeomAbs_Line:++line_count;break;
                case GeomAbs_Circle:++circle_count;break;
                default:break;
                }
            }
            require(line_count==2&&circle_count==2,
                "Custom Fillet face boundary lost its two lines and two circular arcs");
        }
    for(std::size_t index=0;index<edges.size();++index) {
        bool generated_face_found=false;
        for(const auto& descendant:custom.Generated(edges[index]))
            if(descendant.ShapeType()==TopAbs_FACE&&
               BRepAdaptor_Surface(TopoDS::Face(descendant),true).GetType()==
                   GeomAbs_Cylinder)
                generated_face_found=true;
        require(generated_face_found,
            "Custom Fillet adapter omitted edge-to-face ancestry");
    }
    near(volume(custom.shape),volume(reference),
        "Custom planar Fillet volume differs from OCCT");
    near(area(custom.shape),area(reference),
        "Custom planar Fillet area differs from OCCT");
    for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
        TopTools_IndexedMapOfShape custom_topology,reference_topology;
        TopExp::MapShapes(custom.shape,type,custom_topology);
        TopExp::MapShapes(reference,type,reference_topology);
        if(custom_topology.Extent()!=reference_topology.Extent())
            throw std::runtime_error(
                "Custom planar Fillet retained auxiliary result topology type="+
                std::to_string(static_cast<int>(type))+" custom="+
                std::to_string(custom_topology.Extent())+" reference="+
                std::to_string(reference_topology.Extent()));
    }
    TopTools_IndexedMapOfShape custom_edges;
    TopExp::MapShapes(custom.shape,TopAbs_EDGE,custom_edges);
    for(TopExp_Explorer item(input,TopAbs_EDGE);item.More();item.Next()) {
        const auto source=TopoDS::Edge(item.Current());
        const bool touches_selection=std::ranges::any_of(edges,
            [&](const auto& selected) {
                return source.IsSame(selected)||
                    shares_endpoint(source,selected);
            });
        if(!touches_selection)
            require(custom_edges.Contains(source),
                "Custom Fillet rebuilt an unrelated input edge");
    }
    for(int index=1;index<=custom_edges.Extent();++index) {
        const BRepAdaptor_Curve curve(
            TopoDS::Edge(custom_edges.FindKey(index)));
        require(GCPnts_AbscissaPoint::Length(
                    curve,curve.FirstParameter(),curve.LastParameter())>
                    1.0e-7,
            "Custom Fillet produced a degenerate result edge");
    }
}

void verify_circular_result(const TopoDS_Shape& input,
        const TopoDS_Edge& edge,double radius,
        const zima::kernel::OcctPlanarEdgeEndFaces& ends={}) {
    const std::array edges{edge};
    const std::array end_faces{ends};
    const auto custom=zima::kernel::occt_planar_edge_fillet(input,edges,
        radius,1.0e-7,end_faces);
    const auto reference=reference_fillet(input,{edge},radius);
    require(custom.history&&
            BRepCheck_Analyzer(custom.shape,true,true,false).IsValid()&&
            custom.analytic_removal_domains.size()==1&&
            custom.analytic_removal_domains.front().path_kind==
                zima::kernel::EdgeFilletPathKind::Circular&&
            custom.analytic_removal_domains.front().path_radius>0.&&
            custom.analytic_removal_domains.front().sweep_angle_radians>0.&&
            custom.fillet_faces.size()==1&&
            !custom.fillet_faces.front().empty(),
        "Circular ZIMA Fillet omitted its exact revolved domain or ancestry");
    bool torus=false;
    for(const auto& face:custom.fillet_faces.front()) {
        const BRepAdaptor_Surface surface(face,true);
        if(surface.GetType()==GeomAbs_Torus&&
           std::abs(surface.Torus().MinorRadius()-radius)<1.0e-7)
            torus=true;
    }
    require(torus,"Circular ZIMA Fillet did not publish its exact torus face");
    bool generated=false;
    for(const auto& descendant:custom.Generated(edge))
        generated|=descendant.ShapeType()==TopAbs_FACE;
    require(generated,
        "Circular ZIMA Fillet omitted edge-to-torus ancestry");
    near(volume(custom.shape),volume(reference),
        "Circular ZIMA Fillet volume differs from OCCT");
    near(area(custom.shape),area(reference),
        "Circular ZIMA Fillet area differs from OCCT");
}

zima::kernel::OcctPlanarEdgeEndFaces circular_end_faces(
        const TopoDS_Shape& shape,const TopoDS_Edge& edge) {
    const BRepAdaptor_Curve curve(edge);
    require(curve.GetType()==GeomAbs_Circle,
        "Circular end-face fixture requires a circular path");
    gp_Pnt first_point,last_point;
    gp_Vec first_derivative,last_derivative;
    curve.D1(curve.FirstParameter(),first_point,first_derivative);
    curve.D1(curve.LastParameter(),last_point,last_derivative);
    zima::kernel::OcctPlanarEdgeEndFaces result;
    for(TopExp_Explorer item(shape,TopAbs_FACE);item.More();item.Next()) {
        const auto face=TopoDS::Face(item.Current());
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)continue;
        const auto normal=support.Plane().Axis().Direction();
        const auto accepts=[&](const gp_Pnt& endpoint,
                const gp_Vec& derivative) {
            return support.Plane().Distance(endpoint)<=1.0e-7&&
                std::abs(normal.Dot(gp_Dir(derivative)))>.9&&
                BRepClass_FaceClassifier(face,endpoint,1.0e-7).State()==
                    TopAbs_ON;
        };
        if(accepts(first_point,first_derivative))result.first=face;
        if(accepts(last_point,last_derivative))result.last=face;
    }
    require(!result.first.IsNull()&&!result.last.IsNull()&&
            !result.first.IsSame(result.last),
        "Quarter-cylinder fixture lost its two radial end faces");
    return result;
}

TopoDS_Shape triangular_prism(double angle_degrees,double length,
        double height) {
    const double angle=angle_degrees*std::numbers::pi/180.;
    const gp_Pnt origin(0,0,0),first(length,0,0),
        second(length*std::cos(angle),length*std::sin(angle),0);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(origin,first).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(first,second).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(second,origin).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone(),"Triangular prism fixture failed");
    return prism.Shape();
}

TopoDS_Shape concave_prism(double height) {
    const std::array points{gp_Pnt(0,0,0),gp_Pnt(30,0,0),
        gp_Pnt(30,10,0),gp_Pnt(10,10,0),gp_Pnt(10,30,0),
        gp_Pnt(0,30,0)};
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t index=0;index<points.size();++index)
        wire.Add(BRepBuilderAPI_MakeEdge(points[index],
            points[(index+1)%points.size()]).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone(),"Concave prism fixture failed");
    return prism.Shape();
}

struct StraightSplineFixture {
    TopoDS_Shape shape;
    TopoDS_Edge edge;
};

StraightSplineFixture straight_spline_fixture() {
    // This is an exact straight segment with deliberately non-uniform speed:
    // parameter 0.5 lies at x=6, while half of its arc length lies at x=10.
    // A midpoint profile based on the parameter interval therefore creates a
    // shifted tool and this fixture catches that regression.
    TColgp_Array1OfPnt poles(1,3);
    poles.SetValue(1,gp_Pnt(0,0,0));
    poles.SetValue(2,gp_Pnt(2,0,0));
    poles.SetValue(3,gp_Pnt(20,0,0));
    TColStd_Array1OfReal knots(1,2);
    knots.SetValue(1,0.);knots.SetValue(2,1.);
    TColStd_Array1OfInteger multiplicities(1,2);
    multiplicities.SetValue(1,3);multiplicities.SetValue(2,3);
    const Handle(Geom_BSplineCurve) spline=new Geom_BSplineCurve(
        poles,knots,multiplicities,2,false);
    const auto spline_edge=BRepBuilderAPI_MakeEdge(spline).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(spline_edge);
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(20,0,0),gp_Pnt(20,20,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(20,20,0),gp_Pnt(0,20,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(0,20,0),gp_Pnt(0,0,0)).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,12),false,true);
    prism.Build();
    require(prism.IsDone()&&BRepCheck_Analyzer(prism.Shape()).IsValid(),
        "Straight B-spline prism fixture failed");
    TopoDS_Edge result_edge;
    for(TopExp_Explorer item(prism.Shape(),TopAbs_EDGE);
            item.More();item.Next()) {
        const auto candidate=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(candidate);
        if(curve.GetType()!=GeomAbs_BSplineCurve)continue;
        const auto first=curve.Value(curve.FirstParameter());
        const auto last=curve.Value(curve.LastParameter());
        if(std::abs(first.Z())<1.0e-9&&std::abs(last.Z())<1.0e-9) {
            result_edge=candidate;break;
        }
    }
    require(!result_edge.IsNull(),
        "Straight B-spline prism lost its authored path edge");
    return {prism.Shape(),result_edge};
}

struct AngledEndFixture {
    TopoDS_Shape shape;
    TopoDS_Edge edge;
    zima::kernel::OcctPlanarEdgeEndFaces ends;
};

AngledEndFixture angled_end_fixture() {
    AngledEndFixture fixture;
    fixture.shape=BRepPrimAPI_MakeWedge(30.,24.,40.,12.).Shape();
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(fixture.shape,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    for(TopExp_Explorer item(fixture.shape,TopAbs_EDGE);
            item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        const int support_index=edge_faces.FindIndex(edge);
        if(curve.GetType()!=GeomAbs_Line||support_index==0||
           edge_faces.FindFromIndex(support_index).Size()!=2)continue;
        const gp_Dir tangent(gp_Vec(curve.Value(curve.FirstParameter()),
            curve.Value(curve.LastParameter())));
        zima::kernel::OcctPlanarEdgeEndFaces candidate_ends;
        for(TopExp_Explorer face(fixture.shape,TopAbs_FACE);
                face.More();face.Next()) {
            const auto candidate=TopoDS::Face(face.Current());
            bool support=false;
            for(const auto& adjacent:edge_faces.FindFromIndex(support_index))
                support|=candidate.IsSame(adjacent);
            const BRepAdaptor_Surface surface(candidate,true);
            if(support||surface.GetType()!=GeomAbs_Plane)continue;
            const double incidence=std::abs(
                surface.Plane().Axis().Direction().Dot(tangent));
            if(incidence<=0.05||incidence>=0.999)continue;
            const auto first_point=curve.Value(curve.FirstParameter());
            const auto last_point=curve.Value(curve.LastParameter());
            const bool first=surface.Plane().Distance(first_point)<=1.0e-7&&
                BRepClass_FaceClassifier(candidate,first_point,
                    1.0e-7).State()==TopAbs_ON;
            const bool last=surface.Plane().Distance(last_point)<=1.0e-7&&
                BRepClass_FaceClassifier(candidate,last_point,
                    1.0e-7).State()==TopAbs_ON;
            if(first==last)continue;
            (first?candidate_ends.first:candidate_ends.last)=candidate;
        }
        if(!candidate_ends.first.IsNull()||!candidate_ends.last.IsNull()) {
            fixture.edge=edge;fixture.ends=candidate_ends;return fixture;
        }
    }
    throw std::runtime_error(
        "Wedge fixture did not expose a straight edge with an angled end face");
}

struct TangentEndFixture {
    TopoDS_Shape shape;
    std::array<TopoDS_Edge,2> route;
    TopoDS_Face end_face;
};

TangentEndFixture tangent_end_fixture() {
    const gp_Pnt p0(0,0,0),p1(20,0,0),p2(25,5,0),
        p3(25,20,0),p4(0,20,0);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(p0,p1).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Circ(gp_Ax2(gp_Pnt(20,5,0),gp_Dir(0,0,1)),5.),
        -std::numbers::pi/2.,0.).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(p2,p3).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(p3,p4).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(p4,p0).Edge());
    require(wire.IsDone(),
        "Tangent end fixture could not construct its profile");
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,10),false,true);
    prism.Build();
    require(prism.IsDone()&&
            BRepCheck_Analyzer(prism.Shape(),true,true,false).IsValid(),
        "Tangent end fixture could not construct its solid");

    TangentEndFixture result;
    result.shape=prism.Shape();
    for(TopExp_Explorer item(result.shape,TopAbs_EDGE);
            item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        gp_Pnt first,last;
        curve.D0(curve.FirstParameter(),first);
        curve.D0(curve.LastParameter(),last);
        if(std::abs(first.Z())>1.0e-9||std::abs(last.Z())>1.0e-9)
            continue;
        if(curve.GetType()==GeomAbs_Line&&
           ((first.Distance(p0)<1.0e-9&&last.Distance(p1)<1.0e-9)||
            (first.Distance(p1)<1.0e-9&&last.Distance(p0)<1.0e-9)))
            result.route[0]=edge;
        if(curve.GetType()==GeomAbs_Circle&&
           ((first.Distance(p1)<1.0e-9&&last.Distance(p2)<1.0e-9)||
            (first.Distance(p2)<1.0e-9&&last.Distance(p1)<1.0e-9)))
            result.route[1]=edge;
    }
    for(TopExp_Explorer item(result.shape,TopAbs_FACE);
            item.More();item.Next()) {
        const auto candidate=TopoDS::Face(item.Current());
        const BRepAdaptor_Surface support(candidate,true);
        if(support.GetType()!=GeomAbs_Plane||
           support.Plane().Distance(p2)>1.0e-9||
           std::abs(std::abs(support.Plane().Axis().Direction().X())-1.)>
                1.0e-9)
            continue;
        const auto state=BRepClass_FaceClassifier(
            candidate,p2,1.0e-7).State();
        if(state==TopAbs_IN||state==TopAbs_ON) {
            result.end_face=candidate;
            break;
        }
    }
    require(!result.route[0].IsNull()&&!result.route[1].IsNull()&&
            !result.end_face.IsNull(),
        "Tangent end fixture lost its route or terminal face");
    return result;
}

} // namespace

int main() try {
    {
        std::cerr<<"fillet-test: box\n";
        const auto box=BRepPrimAPI_MakeBox(20,30,40).Shape();
        const auto vertical=vertical_edges(box,40);
        require(vertical.size()==4,
            "Box fixture did not expose four vertical edges");
        verify_result(box,{vertical.front()},3.);
        verify_result(box,vertical,2.);
        TopoDS_Edge adjacent;
        for(TopExp_Explorer item(box,TopAbs_EDGE);item.More();item.Next()) {
            const auto candidate=TopoDS::Edge(item.Current());
            if(!candidate.IsSame(vertical.front())&&
               shares_endpoint(candidate,vertical.front())) {
                adjacent=candidate;break;
            }
        }
        require(!adjacent.IsNull(),
            "Box fixture did not expose an adjacent edge");
        const std::array selected{vertical.front(),adjacent};
        const std::array independent_routes{
            std::size_t{1},std::size_t{1}};
        const auto independent=zima::kernel::occt_planar_edge_fillet(
            box,selected,2.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,
            independent_routes);
        require(independent.history&&
                BRepCheck_Analyzer(independent.shape,true,true,false).IsValid(),
            "Custom planar Fillet merged independent routes at one endpoint");
        bool joined_rejected=false;
        try {
            const std::array joined_route{std::size_t{2}};
            static_cast<void>(zima::kernel::occt_planar_edge_fillet(
                box,selected,2.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,
                joined_route));
        } catch(const std::exception&) {
            joined_rejected=true;
        }
        require(joined_rejected,
            "Custom planar Fillet accepted a non-tangent joined route");
    }
    {
        std::cerr<<"fillet-test: wedge\n";
        const auto wedge=triangular_prism(60.,30.,35.);
        const auto vertical=vertical_edges(wedge,35.);
        require(vertical.size()==3,
            "Wedge fixture did not expose three vertical edges");
        const auto origin=std::ranges::find_if(vertical,[](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X())<1.0e-9&&
                std::abs(point.Y())<1.0e-9;
        });
        require(origin!=vertical.end(),
            "Wedge fixture lost its 60-degree edge");
        verify_result(wedge,{*origin},2.5);
    }
    {
        std::cerr<<"fillet-test: concave\n";
        const auto concave=concave_prism(25.);
        const auto vertical=vertical_edges(concave,25.);
        const auto inside=std::ranges::find_if(vertical,[](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X()-10.)<1.0e-9&&
                std::abs(point.Y()-10.)<1.0e-9;
        });
        require(inside!=vertical.end(),
            "Concave prism fixture lost its internal edge");
        verify_result(concave,{*inside},3.,.001);
        const auto outside=std::ranges::find_if(vertical,[](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X())<1.0e-9&&
                std::abs(point.Y())<1.0e-9;
        });
        require(outside!=vertical.end(),
            "Concave prism fixture lost its external edge");
        const std::array orders{
            std::array{*outside,*inside},
            std::array{*inside,*outside}};
        for(const auto& order:orders) {
            const auto mixed=zima::kernel::occt_planar_edge_fillet(
                concave,order,2.,1.0e-7);
            require(mixed.history&&
                    BRepCheck_Analyzer(mixed.shape,true,true,false).IsValid()&&
                    mixed.fillet_faces.size()==order.size()&&
                    std::ranges::all_of(mixed.fillet_faces,
                        [](const auto& faces){return !faces.empty();}),
                "Mixed outer/inner Fillet lost its valid Body, faces or history");
            near(volume(mixed.shape),volume(concave),
                "Mixed outer/inner Fillet did not balance equal-radius CUT and FUSE");
            for(const auto& edge:order) {
                bool generated_face_found=false;
                for(const auto& descendant:mixed.Generated(edge))
                    if(descendant.ShapeType()==TopAbs_FACE&&
                       BRepAdaptor_Surface(TopoDS::Face(descendant),true).
                           GetType()==GeomAbs_Cylinder)
                        generated_face_found=true;
                require(generated_face_found,
                    "Mixed outer/inner Fillet lost edge-to-face ancestry");
            }
        }
    }
    {
        std::cerr<<"fillet-test: circular paths\n";
        for(const double angle:{2.*std::numbers::pi,
                std::numbers::pi/2.}) {
            const auto cylinder=BRepPrimAPI_MakeCylinder(10,20,angle).Shape();
            TopoDS_Edge curved;
            for(TopExp_Explorer edge(cylinder,TopAbs_EDGE);
                    edge.More();edge.Next()) {
                const auto candidate=TopoDS::Edge(edge.Current());
                const BRepAdaptor_Curve curve(candidate);
                if(curve.GetType()==GeomAbs_Circle&&
                   std::abs(curve.Value(curve.FirstParameter()).Z()-20.)<
                        1.0e-9) {
                    curved=candidate;break;
                }
            }
            require(!curved.IsNull(),
                "Cylinder fixture lost its circular top rim");
            verify_circular_result(cylinder,curved,1.);
            if(angle<2.*std::numbers::pi-1.0e-9) {
                const auto ends=circular_end_faces(cylinder,curved);
                verify_circular_result(cylinder,curved,1.,ends);
                auto invalid=ends;
                invalid.first=ends.last;
                bool rejected=false;
                try {
                    const std::array edges{curved};
                    const std::array end_faces{invalid};
                    static_cast<void>(zima::kernel::occt_planar_edge_fillet(
                        cylinder,edges,1.,1.0e-7,end_faces));
                } catch(const std::invalid_argument&) {
                    rejected=true;
                }
                require(rejected,
                    "Circular ZIMA Fillet accepted an end face away from its route endpoint");
            }
        }
    }
    {
        std::cerr<<"fillet-test: straight B-spline path\n";
        const auto fixture=straight_spline_fixture();
        verify_result(fixture.shape,{fixture.edge},1.5);
    }
    {
        std::cerr<<"fillet-test: angled end\n";
        const auto fixture=angled_end_fixture();
        const std::array edges{fixture.edge};
        const std::array ends{fixture.ends};
        const auto result=zima::kernel::occt_planar_edge_fillet(
            fixture.shape,edges,2.,1.0e-7,ends);
        require(BRepCheck_Analyzer(result.Shape(),true,true,false).IsValid()&&
                result.fillet_faces.size()==1&&
                !result.fillet_faces.front().empty()&&
                result.analytic_removal_domains.front().axial_extension>0.,
            "Custom planar Fillet did not trim a valid angled end");
        const auto automatic=zima::kernel::occt_planar_edge_fillet(
            fixture.shape,edges,2.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::
                AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(automatic.Shape(),true,true,false).
                    IsValid()&&
                automatic.analytic_removal_domains.front().
                    axial_extension>0.,
            "Automatic ZIMA Fillet did not detect its angled end face");
        near(volume(automatic.Shape()),volume(result.Shape()),
            "Automatic ZIMA Fillet differs from the explicit angled end");
        near(area(automatic.Shape()),area(result.Shape()),
            "Automatic ZIMA Fillet area differs from the explicit angled end");
    }
    {
        std::cerr<<"fillet-test: tangent Up To end\n";
        const auto fixture=tangent_end_fixture();
        std::array<zima::kernel::OcctPlanarEdgeEndFaces,2> ends{};
        ends[1].last=fixture.end_face;
        const std::array route_sizes{std::size_t{2}};
        const auto result=zima::kernel::occt_planar_edge_fillet(
            fixture.shape,fixture.route,1.,1.0e-7,ends,
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,
            route_sizes);
        require(BRepCheck_Analyzer(result.Shape(),true,true,false).IsValid()&&
                result.fillet_faces.size()==2&&
                std::ranges::all_of(result.fillet_faces,
                    [](const auto& faces){return !faces.empty();}),
            "ZIMA Fillet did not accept a route tangent to its Up To face");
    }
    std::cout<<"Custom planar edge Fillet: convex/concave 90-degree, mixed CUT/FUSE order, batch, 60-degree, perpendicular/angled/tangent Up To ends, exact cylinder ancestry and rejection checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
