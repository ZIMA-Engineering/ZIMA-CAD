#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
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
#include <gp_Elips.hxx>
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
#include <ranges>
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

std::vector<TopoDS_Edge> edges_at_point(const TopoDS_Shape& shape,
        const gp_Pnt& point) {
    std::vector<TopoDS_Edge> result;
    for(TopExp_Explorer item(shape,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        if(std::ranges::none_of(std::views::iota(1,vertices.Extent()+1),
                [&](int index) {
                    return BRep_Tool::Pnt(TopoDS::Vertex(
                        vertices.FindKey(index))).Distance(point)<=1.0e-9;
                }))continue;
        if(std::ranges::none_of(result,[&](const auto& current) {
                return current.IsSame(edge);
            }))result.push_back(edge);
    }
    return result;
}

TopoDS_Edge straight_edge_between(const TopoDS_Shape& shape,
        const gp_Pnt& first,const gp_Pnt& second,double tolerance=1.0e-7) {
    for(TopExp_Explorer item(shape,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        if(curve.GetType()!=GeomAbs_Line)continue;
        const auto a=curve.Value(curve.FirstParameter());
        const auto b=curve.Value(curve.LastParameter());
        if((a.Distance(first)<=tolerance&&b.Distance(second)<=tolerance)||
           (a.Distance(second)<=tolerance&&b.Distance(first)<=tolerance))
            return edge;
    }
    return {};
}

TopoDS_Shape blind_pocket() {
    BRepAlgoAPI_Cut cut(BRepPrimAPI_MakeBox(30,30,30).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(9,9,18),
            gp_Pnt(21,21,31)).Shape());
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull()&&
            BRepCheck_Analyzer(cut.Shape(),true,true,false).IsValid(),
        "Blind-pocket fixture failed");
    return cut.Shape();
}

TopoDS_Shape polygon_prism(const std::vector<gp_Pnt>& points,
        double height,const char* message) {
    require(points.size()>=3,message);
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t index=0;index<points.size();++index)
        wire.Add(BRepBuilderAPI_MakeEdge(points[index],
            points[(index+1)%points.size()]).Edge());
    require(wire.IsDone(),message);
    BRepBuilderAPI_MakeFace face(wire.Wire());
    require(face.IsDone(),message);
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone()&&!prism.Shape().IsNull()&&
            BRepCheck_Analyzer(prism.Shape(),true,true,false).IsValid(),
        message);
    return prism.Shape();
}

TopoDS_Shape split_outer_prism() {
    return polygon_prism({{0,0,0},{10,0,0},{20,0,0},{20,30,0},
        {0,30,0}},40,
        "Split outer prism fixture failed");
}

TopoDS_Shape split_blind_pocket() {
    const auto tool=polygon_prism({{9,9,18},{15,9,18},{21,9,18},
        {21,21,18},{9,21,18}},13,
        "Split blind-pocket tool fixture failed");
    BRepAlgoAPI_Cut cut(BRepPrimAPI_MakeBox(30,30,30).Shape(),tool);
    cut.SetNonDestructive(true);
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull()&&
            BRepCheck_Analyzer(cut.Shape(),true,true,false).IsValid(),
        "Split blind-pocket fixture failed");
    return cut.Shape();
}

TopoDS_Shape through_notch() {
    BRepAlgoAPI_Cut cut(BRepPrimAPI_MakeBox(30,30,30).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-1,9,9),
            gp_Pnt(31,21,21)).Shape());
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull()&&
            BRepCheck_Analyzer(cut.Shape(),true,true,false).IsValid(),
        "Through-notch fixture failed");
    return cut.Shape();
}

TopoDS_Shape side_reaching_pocket() {
    BRepAlgoAPI_Cut cut(BRepPrimAPI_MakeBox(30,30,30).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(9,-1,18),
            gp_Pnt(21,21,31)).Shape());
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull()&&
            BRepCheck_Analyzer(cut.Shape(),true,true,false).IsValid(),
        "Side-reaching pocket fixture failed");
    return cut.Shape();
}

TopoDS_Shape staggered_boxes() {
    BRepAlgoAPI_Fuse fuse(BRepPrimAPI_MakeBox(30,30,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(5,5,10),
            gp_Pnt(25,25,20)).Shape());
    fuse.Build();
    require(fuse.IsDone()&&!fuse.Shape().IsNull()&&
            BRepCheck_Analyzer(fuse.Shape(),true,true,false).IsValid(),
        "Staggered-box fixture failed");
    return fuse.Shape();
}

TopoDS_Shape reference_fillet(const TopoDS_Shape& input,
    const std::vector<TopoDS_Edge>& edges,double radius);

void verify_node(const TopoDS_Shape& input,
        std::vector<TopoDS_Edge> edges,double radius,
        const char* message) {
    std::vector<std::size_t> routes(edges.size(),1);
    const auto custom=zima::kernel::occt_planar_edge_fillet(
        input,edges,radius,1.0e-7,{},
        zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,routes);
    const auto reference=reference_fillet(input,edges,radius);
    require(custom.history&&
            BRepCheck_Analyzer(custom.shape,true,true,false).IsValid()&&
            custom.fillet_faces.size()==edges.size()&&
            std::ranges::all_of(custom.fillet_faces,
                [](const auto& faces) {return !faces.empty();}),message);
    if(edges.size()==3)for(std::size_t index=0;index<edges.size();++index) {
        const bool spherical=std::ranges::any_of(
            custom.fillet_faces[index],[](const auto& face) {
                return BRepAdaptor_Surface(face,true).GetType()==
                    GeomAbs_Sphere;
            });
        const bool generated_sphere=std::ranges::any_of(
            custom.Generated(edges[index]),[](const auto& shape) {
                return shape.ShapeType()==TopAbs_FACE&&
                    BRepAdaptor_Surface(TopoDS::Face(shape),true).
                        GetType()==GeomAbs_Sphere;
            });
        require(spherical&&generated_sphere,
            "Three-branch ZIMA Fillet node lost spherical face ancestry");
    }
    std::reverse(edges.begin(),edges.end());
    const auto reversed=zima::kernel::occt_planar_edge_fillet(
        input,edges,radius,1.0e-7,{},
        zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,routes);
    require(BRepCheck_Analyzer(reversed.shape,true,true,false).IsValid(),
        "Reordered ZIMA Fillet node is invalid");
    near(volume(reversed.shape),volume(custom.shape),
        "ZIMA Fillet node volume depends on selection order");
    near(area(reversed.shape),area(custom.shape),
        "ZIMA Fillet node area depends on selection order");
    near(volume(custom.shape),volume(reference),
        "ZIMA Fillet node volume differs from OCCT");
    near(area(custom.shape),area(reference),
        "ZIMA Fillet node area differs from OCCT");
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

struct HalfTurnRouteFixture {
    TopoDS_Shape shape;
    std::array<TopoDS_Edge,3> route;
};

HalfTurnRouteFixture half_turn_route_fixture() {
    const gp_Pnt first(0,0,0),lower(100,0,0),upper(100,100,0),
        last(0,100,0);
    const auto lower_line=BRepBuilderAPI_MakeEdge(first,lower).Edge();
    const auto half_circle=BRepBuilderAPI_MakeEdge(
        gp_Circ(gp_Ax2(gp_Pnt(100,50,0),gp_Dir(0,0,1)),50.),
        -std::numbers::pi/2.,std::numbers::pi/2.).Edge();
    const auto upper_line=BRepBuilderAPI_MakeEdge(upper,last).Edge();
    const auto closure=BRepBuilderAPI_MakeEdge(last,first).Edge();
    BRepBuilderAPI_MakeWire wire;
    for(const auto& edge:{lower_line,half_circle,upper_line,closure})
        wire.Add(edge);
    require(wire.IsDone(),
        "Half-turn Fillet fixture could not construct its profile");
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,20),false,true);
    prism.Build();
    require(prism.IsDone()&&
            BRepCheck_Analyzer(prism.Shape(),true,true,false).IsValid(),
        "Half-turn Fillet fixture could not construct its solid");
    HalfTurnRouteFixture result{prism.Shape()};
    const std::array starts{gp_Pnt(0,0,20),gp_Pnt(100,0,20),
        gp_Pnt(100,100,20)};
    const std::array ends{gp_Pnt(100,0,20),gp_Pnt(100,100,20),
        gp_Pnt(0,100,20)};
    for(TopExp_Explorer item(result.shape,TopAbs_EDGE);
            item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        const auto a=curve.Value(curve.FirstParameter());
        const auto b=curve.Value(curve.LastParameter());
        for(std::size_t index=0;index<result.route.size();++index) {
            const bool expected=index==1
                ?curve.GetType()==GeomAbs_Circle
                :curve.GetType()==GeomAbs_Line;
            if(expected&&((a.Distance(starts[index])<=1.0e-9&&
                          b.Distance(ends[index])<=1.0e-9)||
                         (a.Distance(ends[index])<=1.0e-9&&
                          b.Distance(starts[index])<=1.0e-9)))
                result.route[index]=edge;
        }
    }
    require(std::ranges::none_of(result.route,
                [](const auto& edge){return edge.IsNull();}),
        "Half-turn Fillet fixture lost a top route edge");
    return result;
}

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

TopoDS_Shape common_solid(const TopoDS_Shape& first,
        const TopoDS_Shape& second,const char* message) {
    BRepAlgoAPI_Common common(first,second);
    common.SetNonDestructive(true);
    common.Build();
    require(common.IsDone()&&!common.Shape().IsNull()&&
            BRepCheck_Analyzer(common.Shape(),true,true,false).IsValid(),
        message);
    return common.Shape();
}

TopoDS_Shape ellipse_prism(double major_radius,double minor_radius,
        double height) {
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(
        gp_Pnt(0,0,0),gp_Dir(0,0,1),gp_Dir(1,0,0)),major_radius,
        minor_radius)).Edge());
    require(wire.IsDone(),"Ellipse rolling-support wire failed");
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone()&&
            BRepCheck_Analyzer(prism.Shape(),true,true,false).IsValid(),
        "Ellipse rolling-support prism failed");
    return prism.Shape();
}

TopoDS_Shape spline_prism(double height) {
    TColgp_Array1OfPnt poles(1,3);
    poles.SetValue(1,gp_Pnt(-100,0,0));
    poles.SetValue(2,gp_Pnt(0,-80,0));
    poles.SetValue(3,gp_Pnt(100,0,0));
    TColStd_Array1OfReal knots(1,2);
    knots.SetValue(1,0.);knots.SetValue(2,1.);
    TColStd_Array1OfInteger multiplicities(1,2);
    multiplicities.SetValue(1,3);multiplicities.SetValue(2,3);
    const Handle(Geom_BSplineCurve) spline=new Geom_BSplineCurve(
        poles,knots,multiplicities,2,false);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(spline).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(100,0,0),gp_Pnt(100,120,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(100,120,0),gp_Pnt(-100,120,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(
        gp_Pnt(-100,120,0),gp_Pnt(-100,0,0)).Edge());
    require(wire.IsDone(),"Spline rolling-support wire failed");
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone()&&
            BRepCheck_Analyzer(prism.Shape(),true,true,false).IsValid(),
        "Spline rolling-support prism failed");
    return prism.Shape();
}

TopoDS_Edge rolling_support_edge(const TopoDS_Shape& shape,double height,
        GeomAbs_SurfaceType first_type,GeomAbs_SurfaceType second_type) {
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    for(int index=1;index<=edge_faces.Extent();++index) {
        const auto edge=TopoDS::Edge(edge_faces.FindKey(index));
        const BRepAdaptor_Curve curve(edge);
        const auto first=curve.Value(curve.FirstParameter());
        const auto last=curve.Value(curve.LastParameter());
        if(std::abs(first.X()-last.X())>1.0e-8||
           std::abs(first.Y()-last.Y())>1.0e-8||
           std::abs(std::abs(first.Z()-last.Z())-height)>1.0e-8)
            continue;
        const auto& adjacent=edge_faces.FindFromIndex(index);
        if(adjacent.Size()!=2)continue;
        std::array<TopoDS_Face,2> faces;
        std::array<GeomAbs_SurfaceType,2> types;
        std::size_t face_index{};
        for(const auto& value:adjacent) {
            faces[face_index]=TopoDS::Face(value);
            types[face_index]=BRepAdaptor_Surface(faces[face_index],true).
                GetType();
            ++face_index;
        }
        if(faces[0].IsSame(faces[1]))continue;
        if(types[0]==GeomAbs_Cylinder&&types[1]==GeomAbs_Cylinder&&
           BRepAdaptor_Surface(faces[0],true).Cylinder().Axis().Location().
                Distance(BRepAdaptor_Surface(faces[1],true).Cylinder().
                    Axis().Location())<1.0e-8)
            continue;
        if((types[0]==first_type&&types[1]==second_type)||
           (types[0]==second_type&&types[1]==first_type))return edge;
    }
    return {};
}

gp_Dir face_normal_on_edge(const TopoDS_Face& face,
        const TopoDS_Edge& edge) {
    Standard_Real first{},last{};
    const auto pcurve=BRep_Tool::CurveOnSurface(edge,face,first,last);
    require(!pcurve.IsNull(),
        "Rolling-support contact lost its face parameter curve");
    const auto uv=pcurve->Value((first+last)/2.);
    gp_Pnt point;gp_Vec du,dv;
    BRepAdaptor_Surface surface(face,true);
    surface.D1(uv.X(),uv.Y(),point,du,dv);
    auto normal=du.Crossed(dv);
    require(normal.SquareMagnitude()>1.0e-18,
        "Rolling-support contact has a degenerate normal");
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return gp_Dir(normal);
}

std::vector<GeomAbs_SurfaceType> rolling_support_contacts(
        const TopoDS_Shape& shape,double fillet_radius,double height) {
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    std::vector<GeomAbs_SurfaceType> result;
    for(int index=1;index<=edge_faces.Extent();++index) {
        const auto edge=TopoDS::Edge(edge_faces.FindKey(index));
        const BRepAdaptor_Curve curve(edge);
        const auto first=curve.Value(curve.FirstParameter());
        const auto last=curve.Value(curve.LastParameter());
        if(std::abs(std::abs(first.Z()-last.Z())-height)>1.0e-7)
            continue;
        const auto& adjacent=edge_faces.FindFromIndex(index);
        if(adjacent.Size()!=2)continue;
        std::array<TopoDS_Face,2> faces;
        std::size_t face_index{};
        for(const auto& value:adjacent)faces[face_index++]=TopoDS::Face(value);
        std::optional<std::size_t> fillet_index;
        for(std::size_t current=0;current<2;++current) {
            const BRepAdaptor_Surface surface(faces[current],true);
            if(surface.GetType()==GeomAbs_Cylinder&&
               std::abs(surface.Cylinder().Radius()-fillet_radius)<1.0e-7)
                fillet_index=current;
        }
        if(!fillet_index)continue;
        const std::size_t support_index=1-*fillet_index;
        const double dot=std::abs(face_normal_on_edge(faces[*fillet_index],
            edge).Dot(face_normal_on_edge(faces[support_index],edge)));
        require(dot>1.-1.0e-9,
            "Rolling-support Fillet is not geometrically tangent");
        result.push_back(BRepAdaptor_Surface(faces[support_index],true).
            GetType());
    }
    return result;
}

void verify_rolling_support(const TopoDS_Shape& input,
        const TopoDS_Edge& edge,double radius,double height,
        std::array<GeomAbs_SurfaceType,2> expected_contacts,
        const char* message) {
    require(!edge.IsNull(),message);
    const auto result=zima::kernel::occt_planar_edge_fillet(
        input,std::array{edge},radius,1.0e-7);
    require(result.history&&result.removal_domains.size()==1&&
            result.analytic_removal_domains.size()==1&&
            result.fillet_faces.size()==1&&
            !result.fillet_faces.front().empty()&&
            BRepCheck_Analyzer(result.shape,true,true,false).IsValid(),
        message);
    TopTools_IndexedMapOfShape tool_faces;
    TopExp::MapShapes(result.removal_domains.front(),TopAbs_FACE,tool_faces);
    require(tool_faces.Extent()==5,
        "Rolling-support Fillet did not use its three-edge exact profile");
    auto contacts=rolling_support_contacts(result.shape,radius,height);
    require(contacts.size()==2,
        "Rolling-support Fillet lost one of its two tangent contacts");
    std::ranges::sort(contacts);
    std::ranges::sort(expected_contacts);
    require(std::ranges::equal(contacts,expected_contacts),message);
    const std::array reversed_edges{TopoDS::Edge(edge.Reversed())};
    const auto reversed=zima::kernel::occt_planar_edge_fillet(input,
        reversed_edges,radius,1.0e-7,{},
        zima::kernel::OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope);
    require(reversed.history&&
            BRepCheck_Analyzer(reversed.shape,true,true,false).IsValid(),
        "Reversed rolling-support Fillet is invalid");
    near(volume(reversed.shape),volume(result.shape),
        "Rolling-support Fillet volume depends on route orientation");
    near(area(reversed.shape),area(result.shape),
        "Rolling-support Fillet area depends on route orientation");
    auto reversed_contacts=rolling_support_contacts(reversed.shape,radius,
        height);
    std::ranges::sort(reversed_contacts);
    require(std::ranges::equal(reversed_contacts,expected_contacts),
        "Reversed rolling-support Fillet lost an exact tangent contact");
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
        const auto origin_vertical=straight_edge_between(box,
            {0,0,0},{0,0,40});
        require(!origin_vertical.IsNull(),
            "Box fixture lost its origin vertical edge");
        const auto first_treatment=zima::kernel::occt_planar_edge_fillet(
            box,std::array{origin_vertical},3.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto approaching=straight_edge_between(first_treatment.shape,
            {3,0,0},{20,0,0});
        require(!approaching.IsNull(),
            "First outer Fillet lost its adjacent standalone edge");
        const auto exact_stop=zima::kernel::occt_planar_edge_fillet(
            first_treatment.shape,std::array{approaching},1.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto automatic_continue=zima::kernel::occt_planar_edge_fillet(
            first_treatment.shape,std::array{approaching},1.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(automatic_continue.shape,true,true,false).
                    IsValid()&&
                automatic_continue.analytic_removal_domains.size()==1&&
                automatic_continue.analytic_removal_domains.front().
                    axial_extension>1.0e-7,
            "Second subtractive Fillet did not continue through the earlier arc to its planar cap");
        require(volume(automatic_continue.shape)<
                volume(exact_stop.shape)-1.0e-6,
            "Second subtractive Fillet did not remove material through the earlier arc");
        const auto compound_approach=straight_edge_between(
            automatic_continue.shape,{0,3,0},{0,30,0});
        require(!compound_approach.IsNull(),
            "Two preceding Fillets lost the remaining compound-terminal edge");
        const auto compound_exact=zima::kernel::occt_planar_edge_fillet(
            automatic_continue.shape,std::array{compound_approach},1.,
            1.0e-7,{},zima::kernel::OcctPlanarEdgeFilletEndMode::
                Perpendicular);
        const auto compound_planar_stop=
            zima::kernel::occt_planar_edge_fillet(
                automatic_continue.shape,std::array{compound_approach},1.,
                1.0e-7,{},zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(compound_planar_stop.shape,true,true,
                    false).IsValid()&&
                compound_planar_stop.analytic_removal_domains.size()==1&&
                compound_planar_stop.analytic_removal_domains.front().
                    axial_extension>1.0e-7,
            "Third subtractive Fillet did not stop on the planar face beyond its compound curved terminal");
        require(volume(compound_planar_stop.shape)<
                volume(compound_exact.shape)-1.0e-6,
            "Third subtractive Fillet crossed its compound terminal without using the adjoining planar stop");
        const auto sequential_pocket=blind_pocket();
        const auto first_inner_edge=straight_edge_between(sequential_pocket,
            {9,9,18},{9,21,18});
        require(!first_inner_edge.IsNull(),
            "Sequential pocket fixture lost its first inner edge");
        const auto first_inner=zima::kernel::occt_planar_edge_fillet(
            sequential_pocket,std::array{first_inner_edge},3.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto approaching_inner=straight_edge_between(first_inner.shape,
            {12,9,18},{21,9,18});
        require(!approaching_inner.IsNull(),
            "First inner Fillet lost its adjacent standalone edge");
        const auto second_inner_exact=zima::kernel::occt_planar_edge_fillet(
            first_inner.shape,std::array{approaching_inner},1.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto second_inner=zima::kernel::occt_planar_edge_fillet(
            first_inner.shape,std::array{approaching_inner},1.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(second_inner.shape,true,true,false).IsValid()&&
                !second_inner.fillet_faces.empty()&&
                !second_inner.fillet_faces.front().empty(),
            "Second additive Fillet ending at an earlier radius is invalid");
        near(volume(second_inner.shape),volume(second_inner_exact.shape),
            "Second additive Fillet crossed the start of an earlier arc");
        near(area(second_inner.shape),area(second_inner_exact.shape),
            "Second additive Fillet changed the earlier arc boundary");
        require(second_inner.analytic_removal_domains.size()==1&&
                second_inner.analytic_removal_domains.front().
                    axial_extension<=1.0e-7,
            "Second additive Fillet continued to a remote end face");
        const auto large_inner_exact=
            zima::kernel::occt_planar_edge_fillet(
                first_inner.shape,std::array{approaching_inner},4.,1.0e-7,
                {},zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto large_inner_curved=
            zima::kernel::occt_planar_edge_fillet(
                first_inner.shape,std::array{approaching_inner},4.,1.0e-7,
                {},zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(large_inner_curved.shape,true,true,
                    false).IsValid()&&
                volume(large_inner_curved.shape)<
                    volume(large_inner_exact.shape)-1.0e-6,
            "Large additive Fillet was not clipped exactly by the preceding radius");
        const std::array connected_route{std::size_t{2}};
        const auto split_outer=split_outer_prism();
        const auto split_outer_vertical=straight_edge_between(split_outer,
            {0,0,0},{0,0,40});
        require(!split_outer_vertical.IsNull(),
            "Split outer prism lost its first Fillet edge");
        const auto first_split_outer=
            zima::kernel::occt_planar_edge_fillet(split_outer,
                std::array{split_outer_vertical},3.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const std::array split_outer_route{
            straight_edge_between(first_split_outer.shape,
                {3,0,0},{10,0,0}),
            straight_edge_between(first_split_outer.shape,
                {10,0,0},{20,0,0})};
        require(std::ranges::none_of(split_outer_route,
                [](const auto& edge){return edge.IsNull();}),
            "Earlier outer Fillet lost its connected split route");
        const auto split_outer_exact=
            zima::kernel::occt_planar_edge_fillet(first_split_outer.shape,
                split_outer_route,1.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,
                connected_route);
        const auto split_outer_automatic=
            zima::kernel::occt_planar_edge_fillet(first_split_outer.shape,
                split_outer_route,1.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope,connected_route);
        require(BRepCheck_Analyzer(split_outer_automatic.shape,true,true,
                    false).IsValid()&&
                split_outer_automatic.analytic_removal_domains.size()==
                    split_outer_route.size()&&
                std::ranges::all_of(
                    split_outer_automatic.analytic_removal_domains,
                    [](const auto& domain) {
                        return domain.axial_extension>1.0e-7;
                    }),
            "Connected subtractive Fillet route did not continue through the earlier arc");
        near(volume(split_outer_exact.shape),volume(exact_stop.shape),
            "Splitting a subtractive Fillet route changed its exact-stop volume");
        near(area(split_outer_exact.shape),area(exact_stop.shape),
            "Splitting a subtractive Fillet route changed its exact-stop area");
        near(volume(split_outer_automatic.shape),
            volume(automatic_continue.shape),
            "Connected subtractive Fillet route changed continuation volume");
        near(area(split_outer_automatic.shape),area(automatic_continue.shape),
            "Connected subtractive Fillet route changed continuation area");

        const auto split_inner=split_blind_pocket();
        const auto split_inner_vertical=straight_edge_between(split_inner,
            {9,9,18},{9,21,18});
        require(!split_inner_vertical.IsNull(),
            "Split inner prism lost its first Fillet edge");
        const auto first_split_inner=
            zima::kernel::occt_planar_edge_fillet(split_inner,
                std::array{split_inner_vertical},3.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const std::array split_inner_route{
            straight_edge_between(first_split_inner.shape,
                {12,9,18},{15,9,18}),
            straight_edge_between(first_split_inner.shape,
                {15,9,18},{21,9,18})};
        require(std::ranges::none_of(split_inner_route,
                [](const auto& edge){return edge.IsNull();}),
            "Earlier inner Fillet lost its connected split route");
        const auto split_inner_exact=
            zima::kernel::occt_planar_edge_fillet(first_split_inner.shape,
                split_inner_route,1.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular,
                connected_route);
        const auto split_inner_automatic=
            zima::kernel::occt_planar_edge_fillet(first_split_inner.shape,
                split_inner_route,1.,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope,connected_route);
        require(BRepCheck_Analyzer(split_inner_automatic.shape,true,true,
                    false).IsValid()&&
                split_inner_automatic.analytic_removal_domains.size()==
                    split_inner_route.size()&&
                std::ranges::all_of(
                    split_inner_automatic.analytic_removal_domains,
                    [](const auto& domain) {
                        return domain.axial_extension<=1.0e-7;
                    }),
            "Connected additive Fillet route continued beyond the earlier arc");
        near(volume(split_inner_automatic.shape),volume(split_inner_exact.shape),
            "Connected additive Fillet crossed the start of an earlier arc");
        near(area(split_inner_automatic.shape),area(split_inner_exact.shape),
            "Connected additive Fillet changed the earlier arc boundary");
        near(volume(split_inner_exact.shape),volume(second_inner_exact.shape),
            "Splitting an additive Fillet route changed its exact-stop volume");
        near(area(split_inner_exact.shape),area(second_inner_exact.shape),
            "Splitting an additive Fillet route changed its exact-stop area");

        const auto cylinder_envelope=
            BRepPrimAPI_MakeCylinder(100.,100.).Shape();
        BRepAlgoAPI_Fuse split_cylinder(
            BRepPrimAPI_MakeCylinder(100.,50.).Shape(),
            BRepPrimAPI_MakeCylinder(
                gp_Ax2(gp_Pnt(0.,0.,50.),gp_Dir(0.,0.,1.)),
                100.,50.).Shape());
        split_cylinder.SetNonDestructive(true);
        split_cylinder.Build();
        require(split_cylinder.IsDone()&&
                BRepCheck_Analyzer(split_cylinder.Shape(),true,true,
                    false).IsValid(),
            "Split cylindrical envelope fixture failed");
        BRepAlgoAPI_Cut notched_cylinder(split_cylinder.Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(-10.,0.,50.),
                gp_Pnt(10.,120.,110.)).Shape());
        notched_cylinder.SetNonDestructive(true);
        notched_cylinder.Build();
        require(notched_cylinder.IsDone()&&
                BRepCheck_Analyzer(notched_cylinder.Shape(),true,true,
                    false).IsValid(),
            "Split cylindrical Fillet terminal fixture failed");
        const double circle_end=std::sqrt(100.*100.-10.*10.);
        const auto cylinder_route=straight_edge_between(
            notched_cylinder.Shape(),{-10.,0.,50.},
            {-10.,circle_end,50.},1.0e-6);
        require(!cylinder_route.IsNull(),
            "Split cylindrical terminal lost its concave route edge");
        const auto flat_cylinder_stop=
            zima::kernel::occt_planar_edge_fillet(
                notched_cylinder.Shape(),std::array{cylinder_route},8.,
                1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto curved_cylinder_stop=
            zima::kernel::occt_planar_edge_fillet(
                notched_cylinder.Shape(),std::array{cylinder_route},8.,
                1.0e-7,{},zima::kernel::
                    OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(curved_cylinder_stop.shape,true,true,
                    false).IsValid()&&
                curved_cylinder_stop.analytic_removal_domains.size()==1&&
                curved_cylinder_stop.analytic_removal_domains.front().
                    axial_extension>1.0e-7&&
                volume(curved_cylinder_stop.shape)>
                    volume(flat_cylinder_stop.shape)+1.0e-6,
            "Additive Fillet did not follow its split cylindrical terminal");
        BRepAlgoAPI_Cut outside_cylinder(curved_cylinder_stop.shape,
            cylinder_envelope);
        outside_cylinder.SetNonDestructive(true);
        outside_cylinder.Build();
        require(outside_cylinder.IsDone()&&
                volume(outside_cylinder.Shape())<=1.0e-6,
            "Split cylindrical terminal let additive Fillet escape its Body envelope");
        BRepAlgoAPI_Cut single_notched_cylinder(cylinder_envelope,
            BRepPrimAPI_MakeBox(gp_Pnt(-10.,0.,50.),
                gp_Pnt(10.,120.,110.)).Shape());
        single_notched_cylinder.SetNonDestructive(true);
        single_notched_cylinder.Build();
        require(single_notched_cylinder.IsDone()&&
                BRepCheck_Analyzer(single_notched_cylinder.Shape(),true,
                    true,false).IsValid(),
            "Single cylindrical Fillet terminal fixture failed");
        const auto single_cylinder_route=straight_edge_between(
            single_notched_cylinder.Shape(),{-10.,0.,50.},
            {-10.,circle_end,50.},1.0e-6);
        require(!single_cylinder_route.IsNull(),
            "Single cylindrical terminal lost its concave route edge");
        const auto single_flat=zima::kernel::occt_planar_edge_fillet(
            single_notched_cylinder.Shape(),
            std::array{single_cylinder_route},8.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto single_curved=zima::kernel::occt_planar_edge_fillet(
            single_notched_cylinder.Shape(),
            std::array{single_cylinder_route},8.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::
                AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(single_curved.shape,true,true,false).
                    IsValid()&&
                single_curved.analytic_removal_domains.size()==1&&
                single_curved.analytic_removal_domains.front().
                    axial_extension>1.0e-7&&
                volume(single_curved.shape)>volume(single_flat.shape)+1.0e-6,
            "Additive Fillet did not reach its single cylindrical terminal");
        BRepAlgoAPI_Cut outside_single(single_curved.shape,
            cylinder_envelope);
        outside_single.SetNonDestructive(true);
        outside_single.Build();
        require(outside_single.IsDone()&&
                volume(outside_single.Shape())<=1.0e-6,
            "Single cylindrical terminal let additive Fillet escape its Body envelope");
        BRepAlgoAPI_Common convex_cylinder_terminal(cylinder_envelope,
            BRepPrimAPI_MakeBox(gp_Pnt(-10.,0.,50.),
                gp_Pnt(10.,120.,100.)).Shape());
        convex_cylinder_terminal.SetNonDestructive(true);
        convex_cylinder_terminal.Build();
        require(convex_cylinder_terminal.IsDone()&&
                BRepCheck_Analyzer(convex_cylinder_terminal.Shape(),true,
                    true,false).IsValid(),
            "Subtractive cylindrical Fillet terminal fixture failed");
        const auto convex_cylinder_route=straight_edge_between(
            convex_cylinder_terminal.Shape(),{-10.,0.,50.},
            {-10.,circle_end,50.},1.0e-6);
        require(!convex_cylinder_route.IsNull(),
            "Subtractive cylindrical terminal lost its Fillet route edge");
        const auto convex_flat=zima::kernel::occt_planar_edge_fillet(
            convex_cylinder_terminal.Shape(),
            std::array{convex_cylinder_route},8.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::Perpendicular);
        const auto convex_automatic=zima::kernel::occt_planar_edge_fillet(
            convex_cylinder_terminal.Shape(),
            std::array{convex_cylinder_route},8.,1.0e-7,{},
            zima::kernel::OcctPlanarEdgeFilletEndMode::
                AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(convex_automatic.shape,true,true,false).
                    IsValid()&&
                convex_automatic.analytic_removal_domains.size()==1&&
                convex_automatic.analytic_removal_domains.front().
                    axial_extension>1.0e-7&&
                volume(convex_automatic.shape)<
                    volume(convex_flat.shape)-1.0e-6,
            "Subtractive Fillet did not reach its cylindrical Body envelope");
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
        const auto outer_node=edges_at_point(box,{0,0,0});
        require(outer_node.size()==3,
            "Box fixture lost its three-branch outer node");
        verify_node(box,std::vector<TopoDS_Edge>(
            outer_node.begin(),outer_node.begin()+2),2.,
            "Two-branch outer ZIMA Fillet node is invalid");
        verify_node(box,outer_node,2.,
            "Three-branch outer ZIMA Fillet node is invalid");
        const auto stepped=staggered_boxes();
        auto shoulder_node=edges_at_point(stepped,{5,5,10});
        std::erase_if(shoulder_node,[](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            return std::abs(curve.Value(curve.FirstParameter()).Z()-
                curve.Value(curve.LastParameter()).Z())>1.0e-9;
        });
        require(shoulder_node.size()==2,
            "Staggered-box fixture lost its two shoulder branches");
        verify_node(stepped,shoulder_node,2.,
            "Staggered-box ZIMA Fillet shoulder node is invalid");
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
        const auto pocket=blind_pocket();
        const auto inner_node=edges_at_point(pocket,{9,9,18});
        require(inner_node.size()==3,
            "Blind-pocket fixture lost its three-branch inner node");
        verify_node(pocket,std::vector<TopoDS_Edge>(
            inner_node.begin(),inner_node.begin()+2),2.,
            "Two-branch inner ZIMA Fillet node is invalid");
        verify_node(pocket,inner_node,2.,
            "Three-branch inner ZIMA Fillet node is invalid");
        const auto through=through_notch();
        const auto through_node=edges_at_point(through,{0,9,9});
        require(through_node.size()==3,
            "Through-notch fixture lost its mouth node");
        const auto through_edge=std::ranges::find_if(through_node,
            [](const auto& edge) {
                const BRepAdaptor_Curve curve(edge);
                const auto first=curve.Value(curve.FirstParameter());
                const auto last=curve.Value(curve.LastParameter());
                return std::abs(first.X()-last.X())>29.;
            });
        require(through_edge!=through_node.end(),
            "Through-notch fixture lost its through edge");
        verify_node(through,{*through_edge},2.,
            "Through-notch ZIMA Fillet terminal is invalid");
        const auto side_pocket=side_reaching_pocket();
        const auto side_node=edges_at_point(side_pocket,{9,0,18});
        require(side_node.size()==3,
            "Side-reaching pocket fixture lost its mouth node");
        const auto side_edge=std::ranges::find_if(side_node,
            [](const auto& edge) {
                const BRepAdaptor_Curve curve(edge);
                const auto first=curve.Value(curve.FirstParameter());
                const auto last=curve.Value(curve.LastParameter());
                return std::abs(first.Y()-last.Y())>20.;
            });
        require(side_edge!=side_node.end(),
            "Side-reaching pocket fixture lost its depth edge");
        verify_node(side_pocket,{*side_edge},2.,
            "Side-reaching pocket ZIMA Fillet terminal is invalid");
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
        std::cerr<<"fillet-test: rolling curved supports\n";
        constexpr double height=40.;
        const auto cylinder=BRepPrimAPI_MakeCylinder(100.,height).Shape();
        const auto half_box=BRepPrimAPI_MakeBox(gp_Pnt(-120.,-120.,0.),
            gp_Pnt(20.,120.,height)).Shape();
        const auto plane_circle=common_solid(cylinder,half_box,
            "Plane-circle rolling-support fixture failed");
        std::cerr<<"  plane-circle\n";
        verify_rolling_support(plane_circle,
            rolling_support_edge(plane_circle,height,GeomAbs_Plane,
                GeomAbs_Cylinder),5.,height,
            {GeomAbs_Plane,GeomAbs_Cylinder},
            "Plane-circle rolling-support Fillet lost a support contact");

        const auto first_circle=BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(-30.,0.,0.),gp_Dir(0.,0.,1.)),100.,height).
            Shape();
        const auto second_circle=BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(30.,0.,0.),gp_Dir(0.,0.,1.)),100.,height).
            Shape();
        const auto circle_circle=common_solid(first_circle,second_circle,
            "Circle-circle rolling-support fixture failed");
        std::cerr<<"  circle-circle\n";
        verify_rolling_support(circle_circle,
            rolling_support_edge(circle_circle,height,GeomAbs_Cylinder,
                GeomAbs_Cylinder),5.,height,
            {GeomAbs_Cylinder,GeomAbs_Cylinder},
            "Circle-circle rolling-support Fillet lost a support contact");

        const auto ellipse=ellipse_prism(120.,80.,height);
        const auto ellipse_circle_source=BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(30.,0.,0.),gp_Dir(0.,0.,1.)),90.,height).
            Shape();
        const auto ellipse_circle=common_solid(ellipse,
            ellipse_circle_source,
            "Ellipse-circle rolling-support fixture failed");
        std::cerr<<"  ellipse-circle\n";
        verify_rolling_support(ellipse_circle,
            rolling_support_edge(ellipse_circle,height,
                GeomAbs_SurfaceOfExtrusion,GeomAbs_Cylinder),3.,height,
            {GeomAbs_SurfaceOfExtrusion,GeomAbs_Cylinder},
            "Ellipse-circle rolling-support Fillet lost a support contact");

        const auto spline=spline_prism(height);
        const auto spline_circle_source=
            BRepPrimAPI_MakeCylinder(80.,height).Shape();
        const auto spline_circle=common_solid(spline,spline_circle_source,
            "Spline-circle rolling-support fixture failed");
        std::cerr<<"  spline-circle\n";
        verify_rolling_support(spline_circle,
            rolling_support_edge(spline_circle,height,
                GeomAbs_SurfaceOfExtrusion,GeomAbs_Cylinder),3.,height,
            {GeomAbs_SurfaceOfExtrusion,GeomAbs_Cylinder},
            "Spline-circle rolling-support Fillet lost a support contact");
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
        for(const double radius:{2.,12.}) {
            const auto explicit_end=zima::kernel::occt_planar_edge_fillet(
                fixture.shape,edges,radius,1.0e-7,ends);
            require(BRepCheck_Analyzer(explicit_end.Shape(),true,true,false).
                        IsValid()&&
                    explicit_end.fillet_faces.size()==1&&
                    !explicit_end.fillet_faces.front().empty()&&
                    explicit_end.analytic_removal_domains.front().
                        axial_extension>0.,
                "Custom planar Fillet did not trim a valid angled end");
            const auto automatic=zima::kernel::occt_planar_edge_fillet(
                fixture.shape,edges,radius,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope);
            require(BRepCheck_Analyzer(automatic.Shape(),true,true,false).
                        IsValid()&&
                    automatic.analytic_removal_domains.front().
                        axial_extension>0.,
                "Automatic ZIMA Fillet did not detect its angled end face");
            near(volume(automatic.Shape()),volume(explicit_end.Shape()),
                "Automatic ZIMA Fillet differs from the explicit angled end");
            near(area(automatic.Shape()),area(explicit_end.Shape()),
                "Automatic ZIMA Fillet area differs from the explicit angled end");
        }
    }
    {
        std::cerr<<"fillet-test: tangent Up To end\n";
        const auto fixture=tangent_end_fixture();
        const std::array route_sizes{std::size_t{2}};
        const std::array separate_route_sizes{
            std::size_t{1},std::size_t{1}};
        for(const double radius:{1.,3.}) {
            const auto result=zima::kernel::occt_planar_edge_fillet(
                fixture.shape,fixture.route,radius,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope,route_sizes);
            const auto separately_selected=
                zima::kernel::occt_planar_edge_fillet(
                    fixture.shape,fixture.route,radius,1.0e-7,{},
                    zima::kernel::OcctPlanarEdgeFilletEndMode::
                        AutomaticBodyEnvelope,separate_route_sizes);
            require(BRepCheck_Analyzer(result.Shape(),true,true,false).IsValid()&&
                    result.fillet_faces.size()==2&&
                    std::ranges::all_of(result.fillet_faces,
                        [](const auto& faces){return !faces.empty();}),
                "ZIMA Fillet did not automatically terminate a route on its tangent material face");
            require(separately_selected.metrics.cutter_solids==1&&
                    BRepCheck_Analyzer(separately_selected.Shape(),true,true,
                        false).IsValid(),
                "Separately selected tangent line and arc were trimmed as two route ends");
            near(volume(separately_selected.Shape()),volume(result.Shape()),
                "Separately selected tangent line and arc changed route volume");
            near(area(separately_selected.Shape()),area(result.Shape()),
                "Separately selected tangent line and arc changed route area");
        }
    }
    {
        std::cerr<<"fillet-test: exact planar half turn\n";
        const auto fixture=half_turn_route_fixture();
        const std::array route_sizes{fixture.route.size()};
        for(const double radius:{1.,6.,12.}) {
            const auto result=zima::kernel::occt_planar_edge_fillet(
                fixture.shape,fixture.route,radius,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope,route_sizes);
            auto reversed_route=fixture.route;
            std::ranges::reverse(reversed_route);
            const auto reversed=zima::kernel::occt_planar_edge_fillet(
                fixture.shape,reversed_route,radius,1.0e-7,{},
                zima::kernel::OcctPlanarEdgeFilletEndMode::
                    AutomaticBodyEnvelope,route_sizes);
            require(result.history&&reversed.history&&
                    BRepCheck_Analyzer(result.Shape(),true,true,false).
                        IsValid()&&
                    BRepCheck_Analyzer(reversed.Shape(),true,true,false).
                        IsValid()&&
                    result.fillet_faces.size()==fixture.route.size()&&
                    std::ranges::all_of(result.fillet_faces,
                        [](const auto& faces){return !faces.empty();}),
                "ZIMA Fillet lost its exact planar half-turn route");
            near(volume(reversed.Shape()),volume(result.Shape()),
                "Half-turn ZIMA Fillet volume depends on route orientation");
            near(area(reversed.Shape()),area(result.Shape()),
                "Half-turn ZIMA Fillet area depends on route orientation");
        }
    }
    std::cout<<"Custom planar edge Fillet: convex/concave 90-degree, inner/outer two/three-branch and staggered-step nodes, through-notch and side-pocket terminals, mixed CUT/FUSE order, batch, 60-degree, perpendicular/angled/tangent Up To ends, exact plane/circle, circle/circle, ellipse/circle and spline/circle rolling supports, cylinder/sphere ancestry and rejection checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
