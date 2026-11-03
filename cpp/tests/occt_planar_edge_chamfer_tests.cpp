#include <zima/kernel/occt_planar_edge_chamfer.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
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

    std::cout<<"Custom planar edge Chamfer: convex/concave A x A, mixed CUT/FUSE order, A x B, A + angle, FLIP, batch, perpendicular/angled ends and exact ancestry checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
