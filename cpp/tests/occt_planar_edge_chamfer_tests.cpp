#include <zima/kernel/occt_planar_edge_chamfer.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
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
           std::abs(std::abs(first.Z()-last.Z())-height)<1.0e-9&&
           std::ranges::none_of(result,[&](const auto& current) {
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
        const gp_Pnt& first,const gp_Pnt& second,
        double tolerance=1.0e-7) {
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

void verify_node(const TopoDS_Shape& input,
        std::vector<TopoDS_Edge> edges,double distance,
        const char* message) {
    const auto custom=zima::kernel::occt_planar_edge_chamfer(
        input,edges,distance,1.0e-7);
    require(BRepCheck_Analyzer(custom.Shape(),true,true,false).IsValid()&&
            custom.chamfer_faces.size()==edges.size()&&
            std::ranges::all_of(custom.chamfer_faces,
                [](const auto& faces) {return !faces.empty();}),message);
    for(const auto& edge:edges) {
        const bool generated=std::ranges::any_of(custom.Generated(edge),
            [](const auto& shape) {
                return shape.ShapeType()==TopAbs_FACE&&
                    BRepAdaptor_Surface(TopoDS::Face(shape),true).
                        GetType()==GeomAbs_Plane;
            });
        require(generated,
            "ZIMA Chamfer node lost edge-to-face ancestry");
    }
    std::reverse(edges.begin(),edges.end());
    const auto reversed=zima::kernel::occt_planar_edge_chamfer(
        input,edges,distance,1.0e-7);
    require(BRepCheck_Analyzer(reversed.Shape(),true,true,false).IsValid(),
        "Reordered ZIMA Chamfer node is invalid");
    near(volume(reversed.Shape()),volume(custom.Shape()),
        "ZIMA Chamfer node depends on selection order");
    BRepFilletAPI_MakeChamfer occt(input);
    for(const auto& edge:edges)occt.Add(distance,edge);
    occt.Build();
    require(occt.IsDone()&&!occt.Shape().IsNull()&&
            BRepCheck_Analyzer(occt.Shape(),true,true,false).IsValid(),
        "OCCT comparison Chamfer node failed");
    near(volume(custom.Shape()),volume(occt.Shape()),
        "ZIMA Chamfer node differs from OCCT");
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

void verify_result(const TopoDS_Shape& input,
        const std::vector<TopoDS_Edge>& edges,double distance,
        double expected_volume,int expected_face_count,
        double tolerance=1.0e-7) {
    const auto custom=zima::kernel::occt_planar_edge_chamfer(
        input,edges,distance,tolerance);
    require(!custom.Shape().IsNull()&&custom.history&&
            BRepCheck_Analyzer(custom.Shape(),true,true,false).IsValid()&&
            custom.treatment_domains.size()==edges.size()&&
            custom.chamfer_faces.size()==edges.size()&&
            std::ranges::all_of(custom.chamfer_faces,
                [](const auto& faces){return !faces.empty();}),
        "Custom planar Chamfer omitted its valid body, history or faces");
    for(const auto& domain:custom.treatment_domains) {
        TopTools_IndexedMapOfShape domain_faces;
        TopExp::MapShapes(domain,TopAbs_FACE,domain_faces);
        require(domain_faces.Extent()==5,
            "Custom Chamfer tool is not one exact triangular prism");
    }
    for(const auto& faces:custom.chamfer_faces)
        for(const auto& face:faces) {
            require(BRepAdaptor_Surface(face,true).GetType()==GeomAbs_Plane,
                "Custom Chamfer face is not an exact plane");
            TopTools_IndexedMapOfShape boundary_edges;
            TopExp::MapShapes(face,TopAbs_EDGE,boundary_edges);
            require(boundary_edges.Extent()==4,
                "Custom Chamfer face is not bounded by four exact lines");
            for(int index=1;index<=boundary_edges.Extent();++index)
                require(BRepAdaptor_Curve(TopoDS::Edge(
                            boundary_edges.FindKey(index))).GetType()==
                            GeomAbs_Line,
                    "Custom Chamfer face contains a non-linear boundary");
        }
    for(std::size_t index=0;index<edges.size();++index) {
        bool generated_face_found=false;
        for(const auto& descendant:custom.Generated(edges[index]))
            if(descendant.ShapeType()==TopAbs_FACE&&
               BRepAdaptor_Surface(TopoDS::Face(descendant),true).GetType()==
                   GeomAbs_Plane)
                generated_face_found=true;
        require(generated_face_found,
            "Custom Chamfer adapter omitted edge-to-face ancestry");
    }
    TopTools_IndexedMapOfShape result_edges;
    TopExp::MapShapes(custom.Shape(),TopAbs_EDGE,result_edges);
    for(int index=1;index<=result_edges.Extent();++index) {
        const BRepAdaptor_Curve curve(
            TopoDS::Edge(result_edges.FindKey(index)));
        require(GCPnts_AbscissaPoint::Length(
                    curve,curve.FirstParameter(),curve.LastParameter())>
                    1.0e-7,
            "Custom Chamfer produced a degenerate result edge");
    }
    near(volume(custom.Shape()),expected_volume,
        "Custom planar Chamfer volume is wrong");
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(custom.Shape(),TopAbs_FACE,result_faces);
    require(result_faces.Extent()==expected_face_count,
        "Custom planar Chamfer retained auxiliary coplanar faces");
}

void verify_asymmetric(const TopoDS_Shape& input,const TopoDS_Edge& edge,
        const TopoDS_Face& distance_a_face,
        zima::kernel::OcctPlanarEdgeChamferMode mode,double distance_a,
        double distance_b_or_angle,double expected_distance_b,
        double edge_length) {
    const std::array selections{
        zima::kernel::OcctPlanarEdgeChamferSelection{
            edge,distance_a_face}};
    const auto custom=zima::kernel::occt_planar_edge_chamfer(
        input,selections,mode,distance_a,distance_b_or_angle,1.0e-7);
    require(BRepCheck_Analyzer(custom.Shape(),true,true,false).IsValid()&&
            custom.chamfer_faces.size()==1&&
            custom.chamfer_faces.front().size()==1,
        "Asymmetric custom Chamfer did not produce one valid exact face");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    const int edge_index=edge_faces.FindIndex(edge);
    require(edge_index!=0,
        "Asymmetric Chamfer fixture edge has no supports");
    TopoDS_Face other_face;
    for(TopTools_ListIteratorOfListOfShape iterator(
            edge_faces.FindFromIndex(edge_index));
        iterator.More();iterator.Next()) {
        const auto candidate=TopoDS::Face(iterator.Value());
        if(!candidate.IsSame(distance_a_face))other_face=candidate;
    }
    require(!other_face.IsNull(),
        "Asymmetric Chamfer fixture lost its second support");
    const auto treatment=custom.chamfer_faces.front().front();
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(treatment,TopAbs_VERTEX,vertices);
    const auto source_line=BRepAdaptor_Curve(edge).Line();
    const auto tangent_distance=[&](const TopoDS_Face& support) {
        const auto plane=BRepAdaptor_Surface(support,true).Plane();
        double found=-1.;
        for(int index=1;index<=vertices.Extent();++index) {
            const auto point=BRep_Tool::Pnt(
                TopoDS::Vertex(vertices.FindKey(index)));
            if(plane.Distance(point)<=1.0e-7)
                found=source_line.Distance(point);
        }
        return found;
    };
    near(tangent_distance(distance_a_face),distance_a,
        "Custom Chamfer lost distance A on its stable support");
    near(tangent_distance(other_face),expected_distance_b,
        "Custom Chamfer calculated the wrong second distance");
    near(volume(custom.Shape()),volume(input)-
            .5*distance_a*expected_distance_b*edge_length,
        "Asymmetric custom Chamfer volume is wrong");
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
            fixture.edge=edge;
            (first?fixture.ends.first:fixture.ends.last)=candidate;
            return fixture;
        }
    }
    throw std::runtime_error(
        "Wedge fixture did not expose a straight edge with an angled end face");
}

} // namespace

int main() try {
    constexpr double box_height=40.;
    constexpr double box_volume=20.*30.*box_height;
    const auto box=BRepPrimAPI_MakeBox(20,30,box_height).Shape();
    const auto box_edges=vertical_edges(box,box_height);
    require(box_edges.size()==4,
        "Box fixture did not expose four vertical edges");
    verify_result(box,{box_edges.front()},3.,
        box_volume-.5*3.*3.*box_height,7,0.001);
    verify_result(box,box_edges,2.,
        box_volume-4.*.5*2.*2.*box_height,10,0.001);
    const auto outer_node=edges_at_point(box,{0,0,0});
    require(outer_node.size()==3,
        "Box fixture lost its three-branch outer node");
    verify_node(box,std::vector<TopoDS_Edge>(
        outer_node.begin(),outer_node.begin()+2),2.,
        "Two-branch outer ZIMA Chamfer node is invalid");
    verify_node(box,outer_node,2.,
        "Three-branch outer ZIMA Chamfer node is invalid");
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
        "Staggered-box ZIMA Chamfer shoulder node is invalid");
    TopTools_IndexedDataMapOfShapeListOfShape box_edge_faces;
    TopExp::MapShapesAndAncestors(box,TopAbs_EDGE,TopAbs_FACE,
        box_edge_faces);
    const int first_edge_index=box_edge_faces.FindIndex(box_edges.front());
    require(first_edge_index!=0,
        "Box fixture lost the first edge supports");
    std::vector<TopoDS_Face> first_edge_faces;
    for(TopTools_ListIteratorOfListOfShape iterator(
            box_edge_faces.FindFromIndex(first_edge_index));
        iterator.More();iterator.Next())
        first_edge_faces.push_back(TopoDS::Face(iterator.Value()));
    require(first_edge_faces.size()==2,
        "Box fixture does not have two edge supports");
    verify_asymmetric(box,box_edges.front(),first_edge_faces.front(),
        zima::kernel::OcctPlanarEdgeChamferMode::TwoDistances,
        3.,5.,5.,box_height);
    constexpr double angle=30.*std::numbers::pi/180.;
    const double angled_distance=3.*std::tan(angle);
    verify_asymmetric(box,box_edges.front(),first_edge_faces.front(),
        zima::kernel::OcctPlanarEdgeChamferMode::DistanceAngle,
        3.,angle,angled_distance,box_height);
    // FLIP is represented by selecting the other stable support face. A must
    // remain attached to that exact face rather than to OCCT enumeration.
    verify_asymmetric(box,box_edges.front(),first_edge_faces.back(),
        zima::kernel::OcctPlanarEdgeChamferMode::DistanceAngle,
        3.,angle,angled_distance,box_height);

    const auto route_x=straight_edge_between(box,{0,0,0},{20,0,0});
    const auto route_y=straight_edge_between(box,{0,0,0},{0,30,0});
    require(!route_x.IsNull()&&!route_y.IsNull(),
        "Asymmetric Chamfer fixture lost its connected route");
    const auto supports_for=[&](const TopoDS_Edge& edge) {
        std::vector<TopoDS_Face> result;
        const int index=box_edge_faces.FindIndex(edge);
        require(index!=0,"Asymmetric Chamfer route edge has no supports");
        for(const auto& value:box_edge_faces.FindFromIndex(index))
            result.push_back(TopoDS::Face(value));
        require(result.size()==2,
            "Asymmetric Chamfer route edge is not manifold");
        return result;
    };
    const auto route_x_faces=supports_for(route_x);
    const auto route_y_faces=supports_for(route_y);
    TopoDS_Face common_route_face;
    for(const auto& first:route_x_faces)
        for(const auto& second:route_y_faces)
            if(first.IsSame(second))common_route_face=first;
    require(!common_route_face.IsNull(),
        "Connected asymmetric Chamfer route has no common support");
    const std::array forward_route{
        zima::kernel::OcctPlanarEdgeChamferSelection{
            route_x,common_route_face},
        zima::kernel::OcctPlanarEdgeChamferSelection{
            route_y,common_route_face}};
    const std::array reverse_route{
        zima::kernel::OcctPlanarEdgeChamferSelection{
            route_y,common_route_face},
        zima::kernel::OcctPlanarEdgeChamferSelection{
            route_x,common_route_face}};
    const auto bent_two_distances=
        zima::kernel::occt_planar_edge_chamfer(box,forward_route,
            zima::kernel::OcctPlanarEdgeChamferMode::TwoDistances,
            3.,5.,1.0e-7);
    const auto reversed_two_distances=
        zima::kernel::occt_planar_edge_chamfer(box,reverse_route,
            zima::kernel::OcctPlanarEdgeChamferMode::TwoDistances,
            3.,5.,1.0e-7);
    require(BRepCheck_Analyzer(bent_two_distances.Shape(),true,true,false).
                IsValid()&&bent_two_distances.chamfer_faces.size()==2&&
            std::ranges::all_of(bent_two_distances.chamfer_faces,
                [](const auto& faces){return !faces.empty();}),
        "Connected bent A x B Chamfer route is invalid");
    near(volume(bent_two_distances.Shape()),
        volume(reversed_two_distances.Shape()),
        "Connected bent A x B Chamfer depends on route order");
    near(area(bent_two_distances.Shape()),
        area(reversed_two_distances.Shape()),
        "Connected bent A x B Chamfer area depends on route order");
    const auto bent_distance_angle=
        zima::kernel::occt_planar_edge_chamfer(box,forward_route,
            zima::kernel::OcctPlanarEdgeChamferMode::DistanceAngle,
            3.,angle,1.0e-7);
    require(BRepCheck_Analyzer(bent_distance_angle.Shape(),true,true,false).
                IsValid()&&bent_distance_angle.chamfer_faces.size()==2,
        "Connected bent A + angle Chamfer route is invalid");

    const auto disconnected_edge=straight_edge_between(box,
        {20,30,0},{20,30,box_height});
    const auto disconnected_faces=supports_for(disconnected_edge);
    bool disconnected_rejected=false;
    try {
        const std::array disconnected{
            zima::kernel::OcctPlanarEdgeChamferSelection{
                route_x,common_route_face},
            zima::kernel::OcctPlanarEdgeChamferSelection{
                disconnected_edge,disconnected_faces.front()}};
        static_cast<void>(zima::kernel::occt_planar_edge_chamfer(
            box,disconnected,
            zima::kernel::OcctPlanarEdgeChamferMode::TwoDistances,
            3.,5.,1.0e-7));
    } catch(const std::invalid_argument&) {
        disconnected_rejected=true;
    }
    require(disconnected_rejected,
        "A x B Chamfer accepted disconnected route members");

    constexpr double concave_height=25.;
    const auto concave=concave_prism(concave_height);
    const auto concave_edges=vertical_edges(concave,concave_height);
    const auto inside=std::ranges::find_if(concave_edges,
        [](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X()-10.)<1.0e-9&&
                std::abs(point.Y()-10.)<1.0e-9;
        });
    require(inside!=concave_edges.end(),
        "Concave prism fixture lost its internal edge");
    verify_result(concave,{*inside},3.,
        volume(concave)+.5*3.*3.*concave_height,9,0.001);
    const auto outside=std::ranges::find_if(concave_edges,
        [](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X())<1.0e-9&&
                std::abs(point.Y())<1.0e-9;
        });
    require(outside!=concave_edges.end(),
        "Concave prism fixture lost its external edge");
    verify_result(concave,{*outside,*inside},3.,volume(concave),10,0.001);
    verify_result(concave,{*inside,*outside},3.,volume(concave),10,0.001);
    const auto pocket=blind_pocket();
    const auto inner_node=edges_at_point(pocket,{9,9,18});
    require(inner_node.size()==3,
        "Concave fixture lost its three-branch inner node");
    verify_node(pocket,std::vector<TopoDS_Edge>(
        inner_node.begin(),inner_node.begin()+2),2.,
        "Two-branch inner ZIMA Chamfer node is invalid");
    verify_node(pocket,inner_node,2.,
        "Three-branch inner ZIMA Chamfer node is invalid");
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
        "Through-notch ZIMA Chamfer terminal is invalid");
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
        "Side-reaching pocket ZIMA Chamfer terminal is invalid");

    {
        const auto fixture=angled_end_fixture();
        const std::array edges{fixture.edge};
        const std::array ends{fixture.ends};
        const auto result=zima::kernel::occt_planar_edge_chamfer(
            fixture.shape,edges,2.,1.0e-7,ends);
        require(BRepCheck_Analyzer(result.Shape(),true,true,false).IsValid()&&
                result.chamfer_faces.size()==1&&
                !result.chamfer_faces.front().empty(),
            "Custom planar Chamfer did not trim a valid angled end");
    }

    {
        // A route can end on a curved Body envelope. The common ZIMA end
        // contract must extend the straight Chamfer profile and clip it by
        // that exact surface, just as it does for a Fillet profile.
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
            "Split cylindrical Chamfer envelope fixture failed");
        BRepAlgoAPI_Cut notched_cylinder(split_cylinder.Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(-10.,0.,50.),
                gp_Pnt(10.,120.,110.)).Shape());
        notched_cylinder.SetNonDestructive(true);
        notched_cylinder.Build();
        require(notched_cylinder.IsDone()&&
                BRepCheck_Analyzer(notched_cylinder.Shape(),true,true,
                    false).IsValid(),
            "Split cylindrical Chamfer terminal fixture failed");
        const double circle_end=std::sqrt(100.*100.-10.*10.);
        const auto cylinder_route=straight_edge_between(
            notched_cylinder.Shape(),{-10.,0.,50.},
            {-10.,circle_end,50.},1.0e-6);
        require(!cylinder_route.IsNull(),
            "Split cylindrical terminal lost its Chamfer route edge");
        const std::array route{cylinder_route};
        const auto perpendicular=zima::kernel::occt_planar_edge_chamfer(
            notched_cylinder.Shape(),route,8.,1.0e-7);
        const auto automatic=zima::kernel::occt_planar_edge_chamfer(
            notched_cylinder.Shape(),route,8.,1.0e-7,{}, {},
            zima::kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(automatic.Shape(),true,true,false).
                    IsValid()&&
                volume(automatic.Shape())>
                    volume(perpendicular.Shape())+1.0e-6,
            "Automatic ZIMA Chamfer did not reach its curved terminal");
        BRepAlgoAPI_Cut outside(automatic.Shape(),cylinder_envelope);
        outside.SetNonDestructive(true);
        outside.Build();
        require(outside.IsDone()&&volume(outside.Shape())<=1.0e-6,
            "Curved terminal let additive Chamfer escape the Body envelope");

        BRepAlgoAPI_Common convex_cylinder_terminal(cylinder_envelope,
            BRepPrimAPI_MakeBox(gp_Pnt(-10.,0.,50.),
                gp_Pnt(10.,120.,100.)).Shape());
        convex_cylinder_terminal.SetNonDestructive(true);
        convex_cylinder_terminal.Build();
        require(convex_cylinder_terminal.IsDone()&&
                BRepCheck_Analyzer(convex_cylinder_terminal.Shape(),true,
                    true,false).IsValid(),
            "Subtractive cylindrical Chamfer terminal fixture failed");
        const auto convex_route=straight_edge_between(
            convex_cylinder_terminal.Shape(),{-10.,0.,50.},
            {-10.,circle_end,50.},1.0e-6);
        require(!convex_route.IsNull(),
            "Subtractive cylindrical terminal lost its Chamfer route edge");
        const std::array convex_selection{convex_route};
        const auto convex_flat=zima::kernel::occt_planar_edge_chamfer(
            convex_cylinder_terminal.Shape(),convex_selection,8.,1.0e-7);
        const auto convex_automatic=zima::kernel::occt_planar_edge_chamfer(
            convex_cylinder_terminal.Shape(),convex_selection,8.,1.0e-7,
            {},{},zima::kernel::OcctPlanarEdgeEndMode::
                AutomaticBodyEnvelope);
        require(BRepCheck_Analyzer(convex_automatic.Shape(),true,true,false).
                    IsValid()&&
                volume(convex_automatic.Shape())<
                    volume(convex_flat.Shape())-1.0e-6,
            "Subtractive Chamfer did not reach its cylindrical Body envelope");
    }

    std::cout<<"Custom planar edge Chamfer: convex/concave A x A, inner/outer two/three-branch and staggered-step nodes, through-notch and side-pocket terminals, mixed CUT/FUSE order, A x B, A + angle, FLIP, batch, perpendicular/angled/curved ends and exact ancestry checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
