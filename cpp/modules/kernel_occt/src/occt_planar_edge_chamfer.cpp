#include <zima/kernel/occt_planar_edge_chamfer.hpp>
#include "occt_edge_treatment_terminals.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <optional>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace zima::kernel {
namespace {

using edge_treatment_terminals::local_curved_terminal_faces;
using edge_treatment_terminals::remote_terminal_faces_through_local_curve;
using edge_treatment_terminals::removal_profile_continues_past_curved_terminal;
using edge_treatment_terminals::route_crosses_curved_terminal;

struct Tool {
    TopoDS_Shape solid;
    std::vector<TopoDS_Face> treatment_faces;
    bool adds_material{};
    TopoDS_Solid material;
    // For asymmetric Chamfers the first support owns A and the second owns B.
    // Keeping the exact faces lets a shared route node prove that A/B continues
    // through the same side instead of choosing each segment independently.
    TopoDS_Face first_support;
    TopoDS_Face second_support;
};

struct SelectedNode {
    TopoDS_Vertex vertex;
    std::vector<std::size_t> tools;
};

struct NodeConnector {
    TopoDS_Shape solid;
    bool adds_material{};
};

struct ContinuousRoute {
    std::vector<std::size_t> selections;
    std::vector<TopoDS_Edge> edges;
    TopoDS_Vertex start_vertex;
    gp_Dir planar_binormal;
};

gp_Dir outgoing_tangent(const TopoDS_Edge& edge,
        const TopoDS_Vertex& vertex,double tolerance) {
    const BRepAdaptor_Curve curve(edge);
    gp_Pnt first,last;
    gp_Vec first_derivative,last_derivative;
    curve.D1(curve.FirstParameter(),first,first_derivative);
    curve.D1(curve.LastParameter(),last,last_derivative);
    const auto point=BRep_Tool::Pnt(vertex);
    gp_Vec direction;
    if(point.Distance(first)<=point.Distance(last)) {
        if(point.Distance(first)>tolerance)throw std::invalid_argument(
            "ZIMA Chamfer node is absent from its branch");
        direction=first_derivative;
    } else {
        if(point.Distance(last)>tolerance)throw std::invalid_argument(
            "ZIMA Chamfer node is absent from its branch");
        direction=-last_derivative;
    }
    if(direction.SquareMagnitude()<=tolerance*tolerance)
        throw std::invalid_argument(
            "ZIMA Chamfer node has a degenerate branch");
    return gp_Dir(direction);
}

TopoDS_Solid make_three_branch_connector(const TopoDS_Vertex& vertex,
        const std::array<gp_Dir,3>& branches,double distance,
        double tolerance) {
    for(std::size_t first=0;first<branches.size();++first)
        for(std::size_t second=first+1;second<branches.size();++second)
            if(std::abs(branches[first].Dot(branches[second]))>1.0e-7)
                throw std::invalid_argument(
                    "ZIMA Chamfer requires orthogonal three-branch nodes");
    const auto origin=BRep_Tool::Pnt(vertex);
    const auto pair_point=[&](std::size_t first,std::size_t second) {
        return origin.Translated(distance*(gp_Vec(branches[first])+
            gp_Vec(branches[second])));
    };
    const std::array base{pair_point(0,1),pair_point(0,2),
        pair_point(1,2)};
    const auto apex=origin.Translated(.5*distance*(
        gp_Vec(branches[0])+gp_Vec(branches[1])+
        gp_Vec(branches[2])));
    const auto triangle=[&](const gp_Pnt& first,const gp_Pnt& second,
            const gp_Pnt& third) {
        BRepBuilderAPI_MakeWire wire;
        wire.Add(BRepBuilderAPI_MakeEdge(first,second).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(second,third).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(third,first).Edge());
        if(!wire.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct a node wire");
        BRepBuilderAPI_MakeFace face(wire.Wire());
        if(!face.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct a node face");
        return face.Face();
    };
    BRepBuilderAPI_Sewing sewing(tolerance);
    sewing.Add(triangle(apex,base[0],base[1]));
    sewing.Add(triangle(apex,base[1],base[2]));
    sewing.Add(triangle(apex,base[2],base[0]));
    sewing.Add(triangle(base[0],base[2],base[1]));
    sewing.Perform();
    TopExp_Explorer shells(sewing.SewedShape(),TopAbs_SHELL);
    if(!shells.More())
        throw std::runtime_error(
            "ZIMA Chamfer could not sew its node connector");
    BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(shells.Current()));
    shells.Next();
    if(shells.More()||!solid.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a valid node connector");
    auto result=solid.Solid();
    if(!result.IsNull())BRepLib::OrientClosedSolid(result);
    if(result.IsNull()||!BRepCheck_Analyzer(result).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a valid node connector");
    return result;
}

TopoDS_Solid make_step_connector(const TopoDS_Vertex& vertex,
        const gp_Dir& first_branch,const gp_Dir& second_branch,
        const gp_Dir& third_branch,double distance,double tolerance) {
    for(const auto pair:std::array{
            std::pair{first_branch,second_branch},
            std::pair{first_branch,third_branch},
            std::pair{second_branch,third_branch}})
        if(std::abs(pair.first.Dot(pair.second))>1.0e-7)
            throw std::invalid_argument(
                "ZIMA Chamfer requires an orthogonal step node");
    const auto origin=BRep_Tool::Pnt(vertex);
    const auto first=origin.Translated(-distance*gp_Vec(first_branch));
    const auto second=origin.Translated(-distance*gp_Vec(second_branch));
    const auto opposite=origin.Translated(-distance*(
        gp_Vec(first_branch)+gp_Vec(second_branch)));
    const auto apex=origin.Translated(distance*gp_Vec(third_branch));
    const auto face=[&](std::initializer_list<gp_Pnt> points) {
        BRepBuilderAPI_MakeWire wire;
        for(auto current=points.begin();current!=points.end();++current) {
            auto next=std::next(current);
            if(next==points.end())next=points.begin();
            wire.Add(BRepBuilderAPI_MakeEdge(*current,*next).Edge());
        }
        if(!wire.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct a step-node wire");
        BRepBuilderAPI_MakeFace result(wire.Wire());
        if(!result.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct a step-node face");
        return result.Face();
    };
    BRepBuilderAPI_Sewing sewing(tolerance);
    sewing.Add(face({origin,second,opposite,first}));
    sewing.Add(face({origin,first,apex}));
    sewing.Add(face({origin,apex,second}));
    sewing.Add(face({first,opposite,apex}));
    sewing.Add(face({second,apex,opposite}));
    sewing.Perform();
    TopExp_Explorer shells(sewing.SewedShape(),TopAbs_SHELL);
    if(!shells.More())
        throw std::runtime_error(
            "ZIMA Chamfer could not sew its step-node connector");
    BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(shells.Current()));
    shells.Next();
    if(shells.More()||!solid.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a step-node connector");
    auto result=solid.Solid();
    if(!result.IsNull())BRepLib::OrientClosedSolid(result);
    if(result.IsNull()||!BRepCheck_Analyzer(result).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a valid step-node connector");
    return result;
}

int material_octants(const TopoDS_Solid& material,const gp_Pnt& origin,
        const std::array<gp_Dir,3>& directions,double probe,
        double tolerance) {
    int inside{};
    for(const int first:{-1,1})for(const int second:{-1,1})
        for(const int third:{-1,1}) {
            const auto point=origin.Translated(probe*(
                first*gp_Vec(directions[0])+
                second*gp_Vec(directions[1])+
                third*gp_Vec(directions[2])));
            BRepClass3d_SolidClassifier classifier(material,point,tolerance);
            if(classifier.State()==TopAbs_IN)++inside;
            else if(classifier.State()!=TopAbs_OUT)
                throw std::invalid_argument(
                    "ZIMA Chamfer could not classify a step node");
        }
    return inside;
}

gp_Dir outward_normal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface support(face,true);
    if(support.GetType()!=GeomAbs_Plane)
        throw std::invalid_argument(
            "ZIMA Chamfer requires two planar supports");
    auto normal=support.Plane().Axis().Direction();
    if(!support.Plane().Position().Direct())normal.Reverse();
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return normal;
}

gp_Dir support_direction(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const gp_Dir& tangent) {
    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())
        if(item.Current().IsSame(edge)) {
            const auto current=item.Current().Orientation();
            if(current!=TopAbs_FORWARD&&current!=TopAbs_REVERSED)
                throw std::invalid_argument(
                    "ZIMA Chamfer support has an invalid edge orientation");
            if(orientation&&*orientation!=current)
                throw std::invalid_argument(
                    "ZIMA Chamfer support uses its edge more than once");
            orientation=current;
        }
    if(!orientation)
        throw std::invalid_argument(
            "ZIMA Chamfer support does not contain its edge");
    auto direction=gp_Vec(outward_normal(face)).Crossed(gp_Vec(tangent));
    if(*orientation==TopAbs_REVERSED)direction.Reverse();
    if(direction.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "ZIMA Chamfer support direction is degenerate");
    return gp_Dir(direction);
}

gp_Dir support_direction_at_point(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const gp_Dir& tangent,
        const gp_Pnt& point,double tolerance) {
    const BRepAdaptor_Surface support(face,true);
    if(support.GetType()==GeomAbs_Plane)
        return support_direction(edge,face,tangent);

    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())
        if(item.Current().IsSame(edge)) {
            const auto current=item.Current().Orientation();
            if(current!=TopAbs_FORWARD&&current!=TopAbs_REVERSED)
                throw std::invalid_argument(
                    "ZIMA Chamfer support has an invalid edge orientation");
            if(orientation&&*orientation!=current)
                throw std::invalid_argument(
                    "ZIMA Chamfer support uses its edge more than once");
            orientation=current;
        }
    if(!orientation)
        throw std::invalid_argument(
            "ZIMA Chamfer support does not contain its edge");

    const BRepAdaptor_Curve2d boundary(edge,face);
    const auto first_uv=boundary.Value(boundary.FirstParameter());
    const auto last_uv=boundary.Value(boundary.LastParameter());
    const auto first_point=support.Value(first_uv.X(),first_uv.Y());
    const auto last_point=support.Value(last_uv.X(),last_uv.Y());
    const auto uv=point.Distance(first_point)<=point.Distance(last_point)
        ?first_uv:last_uv;
    gp_Pnt surface_point;
    gp_Vec first_derivative,second_derivative;
    support.D1(uv.X(),uv.Y(),surface_point,
        first_derivative,second_derivative);
    if(surface_point.Distance(point)>std::max(1.0e-7,10.*tolerance))
        throw std::invalid_argument(
            "ZIMA Chamfer support endpoint is inconsistent");
    auto normal=first_derivative.Crossed(second_derivative);
    if(normal.SquareMagnitude()<=tolerance*tolerance)
        throw std::invalid_argument(
            "ZIMA Chamfer support normal is degenerate");
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    auto direction=normal.Crossed(gp_Vec(tangent));
    if(*orientation==TopAbs_REVERSED)direction.Reverse();
    if(direction.SquareMagnitude()<=tolerance*tolerance)
        throw std::invalid_argument(
            "ZIMA Chamfer support direction is degenerate");
    return gp_Dir(direction);
}

Tool make_tool(const TopoDS_Edge& edge,const TopoDS_Face& first_face,
        const TopoDS_Face& second_face,
        OcctPlanarEdgeChamferMode mode,double distance_a,
        double distance_b_or_angle,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces,
        OcctPlanarEdgeEndMode end_mode,
        std::array<bool,2> route_ends,
        const std::array<TopoDS_Face,2>& node_end_faces) {
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line)
        throw std::invalid_argument(
            "ZIMA Chamfer requires a straight edge");
    const double first_parameter=curve.FirstParameter();
    const double last_parameter=curve.LastParameter();
    if(!std::isfinite(first_parameter)||!std::isfinite(last_parameter)||
       last_parameter<=first_parameter)
        throw std::invalid_argument(
            "ZIMA Chamfer requires a finite edge");
    const gp_Pnt first=curve.Value(first_parameter);
    const gp_Pnt last=curve.Value(last_parameter);
    gp_Vec axis(first,last);
    const double length=axis.Magnitude();
    if(!(length>tolerance))
        throw std::invalid_argument(
            "ZIMA Chamfer edge is too short");
    const gp_Dir tangent(axis);
    const gp_Dir first_direction=support_direction(
        edge,first_face,tangent);
    const gp_Dir second_direction=support_direction(
        edge,second_face,tangent);
    const double angle=first_direction.Angle(second_direction);
    if(!(angle>1.0e-6&&angle<std::numbers::pi-1.0e-6))
        throw std::invalid_argument(
            "ZIMA Chamfer has no finite material wedge");
    gp_Vec bisector=gp_Vec(first_direction)+gp_Vec(second_direction);
    if(bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "ZIMA Chamfer material bisector is degenerate");
    bisector.Normalize();
    double distance_b=distance_a;
    if(mode==OcctPlanarEdgeChamferMode::TwoDistances) {
        distance_b=distance_b_or_angle;
    } else if(mode==OcctPlanarEdgeChamferMode::DistanceAngle) {
        // In the plane perpendicular to the edge, the support rays and the
        // chamfer line form one triangle. A is measured on first_face and the
        // authored angle is between that support ray and the chamfer line.
        // The sine rule yields B; for perpendicular faces this reduces to
        // B=A*tan(angle).
        const double denominator=std::sin(angle+distance_b_or_angle);
        if(!(denominator>1.0e-12))
            throw std::invalid_argument(
                "ZIMA Chamfer angle does not intersect the second support");
        distance_b=distance_a*std::sin(distance_b_or_angle)/denominator;
    }
    if(!std::isfinite(distance_b)||distance_b<=0.)
        throw std::invalid_argument(
            "ZIMA Chamfer calculated an invalid second distance");
    const double probe=std::min(std::min(distance_a,distance_b)*.1,
        std::max(100.*tolerance,length*1.0e-5));
    const auto middle=curve.Value((first_parameter+last_parameter)/2.);
    BRepClass3d_SolidClassifier wedge(material,
        middle.Translated(bisector*probe),tolerance);
    BRepClass3d_SolidClassifier opposite(material,
        middle.Translated(-bisector*probe),tolerance);
    const bool removes=wedge.State()==TopAbs_IN&&
        opposite.State()==TopAbs_OUT;
    const bool adds=wedge.State()==TopAbs_OUT&&
        opposite.State()==TopAbs_IN;
    if(!removes&&!adds)
        throw std::invalid_argument(
            "ZIMA Chamfer could not classify the material wedge");

    std::array<std::vector<TopoDS_Face>,2> terminal_faces;
    std::array<std::vector<TopoDS_Face>,2> curved_terminal_faces;
    const bool explicit_first=!end_faces.first.IsNull()||
        !node_end_faces[0].IsNull();
    const bool explicit_last=!end_faces.last.IsNull()||
        !node_end_faces[1].IsNull();
    if(!end_faces.first.IsNull())terminal_faces[0].push_back(end_faces.first);
    if(!end_faces.last.IsNull())terminal_faces[1].push_back(end_faces.last);
    if(!node_end_faces[0].IsNull())
        terminal_faces[0].push_back(node_end_faces[0]);
    if(!node_end_faces[1].IsNull())
        terminal_faces[1].push_back(node_end_faces[1]);
    const bool automatic_first=end_mode==
        OcctPlanarEdgeEndMode::AutomaticBodyEnvelope&&
        end_faces.first.IsNull()&&route_ends[0];
    const bool automatic_last=end_mode==
        OcctPlanarEdgeEndMode::AutomaticBodyEnvelope&&
        end_faces.last.IsNull()&&route_ends[1];
    if(end_mode==OcctPlanarEdgeEndMode::AutomaticBodyEnvelope) {
        if(terminal_faces[0].empty()&&route_ends[0])
            terminal_faces[0]=
                edge_treatment_terminals::local_terminal_faces(material,
                    first_face,second_face,first,tolerance,false);
        if(terminal_faces[1].empty()&&route_ends[1])
            terminal_faces[1]=
                edge_treatment_terminals::local_terminal_faces(material,
                    first_face,second_face,last,tolerance,false);
        if(route_ends[0])
            curved_terminal_faces[0]=local_curved_terminal_faces(material,
                first_face,second_face,first,tolerance);
        if(route_ends[1])
            curved_terminal_faces[1]=local_curved_terminal_faces(material,
                first_face,second_face,last,tolerance);
    }
    const gp_Dir first_outward(-gp_Vec(tangent));
    const gp_Dir last_outward(tangent);
    const bool local_curved_first=!curved_terminal_faces[0].empty();
    const bool local_curved_last=!curved_terminal_faces[1].empty();
    const bool route_crossing_first=!explicit_first&&local_curved_first&&
        route_crosses_curved_terminal(curved_terminal_faces[0],first,
            first_outward,tolerance);
    const bool route_crossing_last=!explicit_last&&local_curved_last&&
        route_crosses_curved_terminal(curved_terminal_faces[1],last,
            last_outward,tolerance);
    const bool continue_first=local_curved_first&&removes&&
        !explicit_first&&!route_crossing_first&&
        removal_profile_continues_past_curved_terminal(material,first,
            first_outward,first_direction,second_direction,
            std::max(distance_a,distance_b),tolerance);
    const bool continue_last=local_curved_last&&removes&&
        !explicit_last&&!route_crossing_last&&
        removal_profile_continues_past_curved_terminal(material,last,
            last_outward,first_direction,second_direction,
            std::max(distance_a,distance_b),tolerance);
    if(continue_first)
        terminal_faces[0]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,first,first_outward,tolerance);
    if(continue_last)
        terminal_faces[1]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,last,last_outward,tolerance);
    const bool remote_first=continue_first&&!terminal_faces[0].empty();
    const bool remote_last=continue_last&&!terminal_faces[1].empty();
    const auto axial_extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,const gp_Dir& outward,
            double cross_span) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)return cross_span;
        const auto normal=support.Plane().Axis().Direction();
        const double signed_incidence=normal.Dot(outward);
        const double incidence=std::abs(signed_incidence);
        if(support.Plane().Distance(endpoint)>tolerance) {
            if(incidence<=1.0e-8)
                throw std::invalid_argument(
                    "ZIMA Chamfer remote end face does not intersect the route");
            const double distance=gp_Vec(endpoint,
                support.Plane().Location()).Dot(gp_Vec(normal)) /
                signed_incidence;
            if(distance<=tolerance)
                throw std::invalid_argument(
                    "ZIMA Chamfer remote end face is behind the route endpoint");
            return distance+cross_span/incidence+tolerance;
        }
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "ZIMA Chamfer end face does not contain the route endpoint");
        if(incidence<=1.0e-8)
            return cross_span;
        return cross_span/incidence+tolerance;
    };
    // The exact Chamfer profile is one triangle: the authored treatment line
    // and the two support-face segments meeting at the source edge. Closing
    // it outside the corner creates unnecessary coplanar seams and can retain
    // the auxiliary overlap as visible material for an additive Chamfer.
    const double cross_span=2.*(distance_a+distance_b);
    const auto end_extension=[&](const auto& faces,const gp_Pnt& endpoint,
            const gp_Dir& outward) {
        double result{};
        for(const auto& face:faces)
            result=std::max(result,
                axial_extension(face,endpoint,outward,cross_span));
        return result;
    };
    const double first_extension=end_extension(terminal_faces[0],first,
        first_outward);
    const double last_extension=end_extension(terminal_faces[1],last,
        last_outward);
    const gp_Pnt origin=first.Translated(
        -gp_Vec(tangent)*first_extension);
    const gp_Pnt first_tangent=origin.Translated(
        gp_Vec(first_direction)*distance_a);
    const gp_Pnt second_tangent=origin.Translated(
        gp_Vec(second_direction)*distance_b);
    const auto treatment_edge=
        BRepBuilderAPI_MakeEdge(first_tangent,second_tangent).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(treatment_edge);
    wire.Add(BRepBuilderAPI_MakeEdge(second_tangent,origin).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(origin,first_tangent).Edge());
    if(!wire.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct its tool wire");
    BRepBuilderAPI_MakeFace section(wire.Wire());
    if(!section.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct its tool section");
    BRepPrimAPI_MakePrism prism(section.Face(),
        gp_Vec(tangent)*(length+first_extension+last_extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct a valid tool");
    TopoDS_Shape tool=prism.Shape();
    std::vector<TopoDS_Face> treatment_faces;
    for(const auto& generated:prism.Generated(treatment_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            treatment_faces.push_back(TopoDS::Face(generated));
    if(treatment_faces.empty())
        throw std::runtime_error(
            "ZIMA Chamfer tool omitted its treatment face");
    const auto clip=[&](const TopoDS_Face& face,const gp_Pnt& inside) {
        if(face.IsNull())return;
        const BRepAdaptor_Surface end_support(face,true);
        TopoDS_Face boundary=face;
        if(end_support.GetType()==GeomAbs_Plane) {
            BRepBuilderAPI_MakeFace complete(end_support.Plane());
            if(!complete.IsDone())
                throw std::runtime_error(
                    "ZIMA Chamfer could not construct an end plane");
            boundary=complete.Face();
        } else if(end_support.GetType()==GeomAbs_Cylinder) {
            BRepBuilderAPI_MakeFace complete(end_support.Cylinder());
            if(complete.IsDone())boundary=complete.Face();
        } else if(end_support.GetType()==GeomAbs_Cone) {
            BRepBuilderAPI_MakeFace complete(end_support.Cone());
            if(complete.IsDone())boundary=complete.Face();
        } else if(end_support.GetType()==GeomAbs_Sphere) {
            BRepBuilderAPI_MakeFace complete(end_support.Sphere());
            if(complete.IsDone())boundary=complete.Face();
        } else if(end_support.GetType()==GeomAbs_Torus) {
            BRepBuilderAPI_MakeFace complete(end_support.Torus());
            if(complete.IsDone())boundary=complete.Face();
        }
        gp_Pnt retained_side=inside;
        BRepClass3d_SolidClassifier retained(tool,retained_side,tolerance);
        if(retained.State()!=TopAbs_IN) {
            Bnd_Box bounds;
            BRepBndLib::Add(tool,bounds);
            double x0{},y0{},z0{},x1{},y1{},z1{};
            bounds.Get(x0,y0,z0,x1,y1,z1);
            const double diagonal=gp_Pnt(x0,y0,z0).Distance(
                gp_Pnt(x1,y1,z1));
            const double step=std::max(1000.*tolerance,
                diagonal*1.0e-6);
            bool found{};
            for(const double multiplier:{1.,10.,100.}) {
                for(int x=-1;x<=1&&!found;++x)
                    for(int y=-1;y<=1&&!found;++y)
                        for(int z=-1;z<=1&&!found;++z) {
                            if(x==0&&y==0&&z==0)continue;
                            const auto candidate=inside.Translated(gp_Vec(
                                x*step*multiplier,y*step*multiplier,
                                z*step*multiplier));
                            BRepClass3d_SolidClassifier classifier(
                                tool,candidate,tolerance);
                            if(classifier.State()==TopAbs_IN) {
                                retained_side=candidate;
                                found=true;
                            }
                        }
                if(found)break;
            }
            if(!found)
                throw std::runtime_error(
                    "ZIMA Chamfer could not trim its selected end face");
        }
        BRepPrimAPI_MakeHalfSpace half_space(boundary,retained_side);
        if(!half_space.IsDone())
            throw std::runtime_error(
                "ZIMA Chamfer could not construct an end half-space");
        BRepAlgoAPI_Common common(tool,half_space.Solid());
        common.SetNonDestructive(true);
        common.SetFuzzyValue(tolerance);
        common.Build();
        if(!common.IsDone()||common.Shape().IsNull()||
           !BRepCheck_Analyzer(common.Shape()).IsValid())
            throw std::runtime_error(
                "ZIMA Chamfer could not trim its selected end face");
        TopTools_IndexedMapOfShape result_faces;
        TopExp::MapShapes(common.Shape(),TopAbs_FACE,result_faces);
        std::vector<TopoDS_Face> clipped_faces;
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto candidate=TopoDS::Face(shape);
            if(std::ranges::none_of(clipped_faces,[&](const auto& current) {
                    return current.IsSame(candidate);
                }))clipped_faces.push_back(candidate);
        };
        for(const auto& treatment_face:treatment_faces) {
            append(treatment_face);
            for(const auto& descendant:common.Modified(treatment_face))
                append(descendant);
            for(const auto& descendant:common.Generated(treatment_face))
                append(descendant);
        }
        // Preserve the exact prism-side support if OCCT omits Common history
        // for an angled trim. All fragments sharing that analytic surface are
        // descendants of this authored treatment face.
        if(clipped_faces.empty())
            for(int index=1;index<=result_faces.Extent();++index) {
                const auto candidate=TopoDS::Face(
                    result_faces.FindKey(index));
                if(std::ranges::any_of(treatment_faces,
                        [&](const auto& source) {
                            return BRep_Tool::Surface(source)==
                                BRep_Tool::Surface(candidate);
                        }))append(candidate);
            }
        if(clipped_faces.empty())
            throw std::runtime_error(
                "ZIMA Chamfer trim lost its treatment face");
        treatment_faces=std::move(clipped_faces);
        tool=common.Shape();
    };
    const gp_Pnt interior=first.Translated(gp_Vec(tangent)*(length*.5));
    // A subtractive automatic end needs only a finite overlap.  The final
    // Body CUT is its exact envelope clip; pre-intersecting the tool with the
    // same material creates coincident boundary faces which OCCT can discard.
    // Explicit Up To faces and every additive end still require their own
    // half-space clip before the Boolean operation.
    if(adds||!automatic_first||remote_first)
        for(const auto& face:terminal_faces[0])clip(face,interior);
    if(adds||!automatic_last||remote_last)
        for(const auto& face:terminal_faces[1])clip(face,interior);
    return {tool,std::move(treatment_faces),adds,material,
        first_face,second_face};
}

std::optional<ContinuousRoute> find_continuous_route(
        std::span<const OcctPlanarEdgeChamferSelection> selections,
        const std::vector<SelectedNode>& nodes,
        const std::vector<std::array<TopoDS_Face,2>>& faces,
        const std::vector<TopoDS_Solid>& materials,
        OcctPlanarEdgeChamferMode mode,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        OcctPlanarEdgeEndMode end_mode,double tolerance) {
    // Keep all existing straight-edge terminal, miter and branch behavior.
    // This path is only for one complete tangent line/circle route whose
    // triangular section can be transported without changing material sides.
    if(mode!=OcctPlanarEdgeChamferMode::EqualDistance||
       (end_mode!=OcctPlanarEdgeEndMode::Perpendicular&&
        end_mode!=OcctPlanarEdgeEndMode::AutomaticBodyEnvelope)||
       selections.size()<2||faces.size()!=selections.size()||
       materials.size()!=selections.size()||
       (!end_faces.empty()&&std::ranges::any_of(end_faces,
            [](const auto& ends) {
                return !ends.first.IsNull()||!ends.last.IsNull();
            })))return {};
    bool has_circle{};
    for(std::size_t index=0;index<selections.size();++index) {
        const BRepAdaptor_Curve curve(selections[index].edge);
        if(curve.GetType()!=GeomAbs_Line&&curve.GetType()!=GeomAbs_Circle)
            return {};
        has_circle=has_circle||curve.GetType()==GeomAbs_Circle;
        if(!materials[index].IsSame(materials.front()))return {};
    }
    if(!has_circle)return {};

    std::vector<const SelectedNode*> terminals;
    for(const auto& node:nodes) {
        if(node.tools.size()==1)terminals.push_back(&node);
        else if(node.tools.size()!=2)return {};
        if(node.tools.size()==2) {
            const auto first=outgoing_tangent(
                selections[node.tools[0]].edge,node.vertex,tolerance);
            const auto second=outgoing_tangent(
                selections[node.tools[1]].edge,node.vertex,tolerance);
            if(!first.IsParallel(second,std::max(1.0e-9,tolerance)))
                return {};
        }
    }
    if(terminals.size()!=2)return {};
    const auto usable_start=[&](const SelectedNode& terminal) {
        if(BRepAdaptor_Curve(
                selections[terminal.tools.front()].edge).GetType()!=
                GeomAbs_Line)return false;
        return std::ranges::any_of(
            faces[terminal.tools.front()],[](const auto& face) {
                return BRepAdaptor_Surface(face,true).GetType()==
                    GeomAbs_Plane;
            });
    };
    const SelectedNode* start=usable_start(*terminals[0])
        ?terminals[0]:(usable_start(*terminals[1])?terminals[1]:nullptr);
    if(!start)return {};
    const SelectedNode* finish=start==terminals[0]
        ?terminals[1]:terminals[0];

    ContinuousRoute result;
    result.start_vertex=start->vertex;
    std::vector<bool> visited(selections.size());
    auto current=start->vertex;
    while(true) {
        const auto node=std::ranges::find_if(nodes,[&](const auto& value) {
            return value.vertex.IsSame(current);
        });
        if(node==nodes.end())return {};
        std::vector<std::size_t> next;
        for(const auto index:node->tools)
            if(!visited[index])next.push_back(index);
        if(next.empty())break;
        if(next.size()!=1)return {};
        const auto index=next.front();
        auto edge=selections[index].edge;
        TopoDS_Vertex first,last;
        TopExp::Vertices(edge,first,last,true);
        if(first.IsNull()||last.IsNull())return {};
        if(first.IsSame(current)) {
            current=last;
        } else if(last.IsSame(current)) {
            edge.Reverse();
            current=first;
        } else {
            return {};
        }
        visited[index]=true;
        result.selections.push_back(index);
        result.edges.push_back(edge);
    }
    if(result.selections.size()!=selections.size()||
       !current.IsSame(finish->vertex))return {};

    const auto& anchor_faces=faces[result.selections.front()];
    for(const auto& candidate:anchor_faces) {
        const BRepAdaptor_Surface candidate_surface(candidate,true);
        if(candidate_surface.GetType()!=GeomAbs_Plane)continue;
        const auto candidate_plane=candidate_surface.Plane();
        const auto candidate_normal=outward_normal(candidate);
        const bool common=std::ranges::all_of(result.selections,
            [&](const auto index) {
                return std::ranges::any_of(faces[index],
                    [&](const auto& support) {
                        const BRepAdaptor_Surface surface(support,true);
                        return surface.GetType()==GeomAbs_Plane&&
                            candidate_plane.Axis().Direction().IsParallel(
                                surface.Plane().Axis().Direction(),1.0e-9)&&
                            candidate_plane.Distance(
                                surface.Plane().Location())<=
                                std::max(1.0e-7,10.*tolerance)&&
                            candidate_normal.Dot(outward_normal(support))>
                                1.-1.0e-9;
                    });
            });
        if(common) {
            result.planar_binormal=candidate_normal;
            return result;
        }
    }
    return {};
}

std::optional<Tool> make_collapsed_round_route_tool(
        const ContinuousRoute& route,
        std::span<const OcctPlanarEdgeChamferSelection> selections,
        const std::vector<std::array<TopoDS_Face,2>>& selection_faces,
        const std::vector<TopoDS_Solid>& materials,double distance,
        double tolerance) {
    // Offsetting a convex line-arc-line boundary towards the material shrinks
    // its circular member. Once the authored Chamfer distance reaches that
    // radius, the offset arc vanishes. Treat the two remaining lines as
    // independent branches: each branch uses the same curved-terminal analysis
    // as a Fillet, continues through the consumed arc to its real adjacent cap,
    // and is clipped there. Do not invent a virtual sharp corner; that shortcut
    // leaves a tail whenever the actual terminal is oblique or non-orthogonal.
    if(route.selections.size()!=3||route.edges.size()!=3)return {};
    const auto first_selection=route.selections.front();
    const auto arc_selection=route.selections[1];
    const auto last_selection=route.selections.back();
    const auto& first_edge=selections[first_selection].edge;
    const auto& arc_edge=selections[arc_selection].edge;
    const auto& last_edge=selections[last_selection].edge;
    const BRepAdaptor_Curve first_curve(first_edge);
    const BRepAdaptor_Curve arc_curve(arc_edge);
    const BRepAdaptor_Curve last_curve(last_edge);
    if(first_curve.GetType()!=GeomAbs_Line||
       arc_curve.GetType()!=GeomAbs_Circle||
       last_curve.GetType()!=GeomAbs_Line)return {};

    TopoDS_Face common_face;
    for(const auto& candidate:selection_faces[arc_selection]) {
        const BRepAdaptor_Surface surface(candidate,true);
        if(surface.GetType()==GeomAbs_Plane&&
           outward_normal(candidate).Dot(route.planar_binormal)>1.-1.0e-9) {
            common_face=candidate;
            break;
        }
    }
    if(common_face.IsNull())return {};
    const double middle_parameter=.5*(arc_curve.FirstParameter()+
        arc_curve.LastParameter());
    gp_Pnt arc_middle;
    gp_Vec arc_derivative;
    arc_curve.D1(middle_parameter,arc_middle,arc_derivative);
    if(arc_derivative.SquareMagnitude()<=tolerance*tolerance)return {};
    const auto circle=arc_curve.Circle();
    gp_Vec toward_center(arc_middle,circle.Location());
    if(toward_center.SquareMagnitude()<=tolerance*tolerance)return {};
    const auto material_direction=support_direction_at_point(arc_edge,
        common_face,gp_Dir(arc_derivative),arc_middle,tolerance);
    if(material_direction.Dot(gp_Dir(toward_center))<=1.0e-8||
       distance<circle.Radius()-tolerance)return {};

    const auto route_end_at_round=[&](const TopoDS_Edge& line,
            const TopoDS_Edge& round)->std::optional<std::array<bool,2>> {
        TopoDS_Vertex line_first,line_last;
        TopExp::Vertices(line,line_first,line_last,true);
        if(line_first.IsNull()||line_last.IsNull())return {};
        TopTools_IndexedMapOfShape round_vertices;
        TopExp::MapShapes(round,TopAbs_VERTEX,round_vertices);
        const bool first_shared=round_vertices.Contains(line_first);
        const bool last_shared=round_vertices.Contains(line_last);
        if(first_shared==last_shared)return {};
        return std::array<bool,2>{first_shared,last_shared};
    };
    const auto first_route_ends=route_end_at_round(first_edge,arc_edge);
    const auto last_route_ends=route_end_at_round(last_edge,arc_edge);
    if(!first_route_ends||!last_route_ends)return {};

    const auto first_tool=make_tool(first_edge,
        selection_faces[first_selection][0],
        selection_faces[first_selection][1],
        OcctPlanarEdgeChamferMode::EqualDistance,distance,distance,
        tolerance,materials[first_selection],{},
        OcctPlanarEdgeEndMode::AutomaticBodyEnvelope,*first_route_ends,{});
    const auto last_tool=make_tool(last_edge,
        selection_faces[last_selection][0],
        selection_faces[last_selection][1],
        OcctPlanarEdgeChamferMode::EqualDistance,distance,distance,
        tolerance,materials[last_selection],{},
        OcctPlanarEdgeEndMode::AutomaticBodyEnvelope,*last_route_ends,{});
    if(first_tool.adds_material!=last_tool.adds_material||
       !first_tool.material.IsSame(last_tool.material))return {};

    BRepAlgoAPI_Fuse fuse(first_tool.solid,last_tool.solid);
    fuse.SetNonDestructive(true);
    fuse.SetRunParallel(false);
    fuse.SetToFillHistory(true);
    fuse.SetFuzzyValue(tolerance);
    fuse.Build();
    if(!fuse.IsDone()||fuse.Shape().IsNull()||
       !BRepCheck_Analyzer(fuse.Shape()).IsValid())return {};
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,result_faces);
    std::vector<TopoDS_Face> treatment_faces;
    const auto append=[&](const TopoDS_Shape& shape) {
        if(shape.ShapeType()!=TopAbs_FACE||
           !result_faces.Contains(shape))return;
        const auto face=TopoDS::Face(shape);
        if(std::ranges::none_of(treatment_faces,[&](const auto& current) {
                return current.IsSame(face);
            }))treatment_faces.push_back(face);
    };
    for(const auto* source:{&first_tool,&last_tool})
        for(const auto& face:source->treatment_faces) {
            append(face);
            for(const auto& descendant:fuse.Modified(face))
                append(descendant);
            for(const auto& descendant:fuse.Generated(face))
                append(descendant);
        }
    if(treatment_faces.empty())return {};
    return Tool{fuse.Shape(),std::move(treatment_faces),
        first_tool.adds_material,first_tool.material,
        first_tool.first_support,last_tool.second_support};
}

Tool make_continuous_route_tool(const ContinuousRoute& route,
        std::span<const OcctPlanarEdgeChamferSelection> selections,
        const std::vector<std::array<TopoDS_Face,2>>& selection_faces,
        const std::vector<TopoDS_Solid>& materials,double distance,
        double tolerance) {
    if(route.selections.empty()||route.edges.empty())
        throw std::invalid_argument("ZIMA Chamfer route is empty");
    if(const auto collapsed=make_collapsed_round_route_tool(route,
            selections,selection_faces,materials,distance,tolerance))
        return *collapsed;
    const auto anchor=route.selections.front();
    const auto& edge=selections[anchor].edge;
    const auto& first_face=selection_faces[anchor][0];
    const auto& second_face=selection_faces[anchor][1];
    const auto& material=materials[anchor];
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line)
        throw std::invalid_argument(
            "ZIMA Chamfer route requires a straight terminal member");
    const auto first=curve.Value(curve.FirstParameter());
    const auto last=curve.Value(curve.LastParameter());
    const gp_Vec axis(first,last);
    if(axis.Magnitude()<=tolerance)
        throw std::invalid_argument("ZIMA Chamfer edge is too short");
    const gp_Dir tangent(axis);
    const auto origin=BRep_Tool::Pnt(route.start_vertex);
    const auto first_direction=support_direction_at_point(
        edge,first_face,tangent,origin,tolerance);
    const auto second_direction=support_direction_at_point(
        edge,second_face,tangent,origin,tolerance);
    const double angle=first_direction.Angle(second_direction);
    if(!(angle>1.0e-6&&angle<std::numbers::pi-1.0e-6))
        throw std::invalid_argument(
            "ZIMA Chamfer has no finite material wedge");
    gp_Vec bisector=gp_Vec(first_direction)+gp_Vec(second_direction);
    if(bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "ZIMA Chamfer material bisector is degenerate");
    bisector.Normalize();
    const double probe=std::min(distance*.1,
        std::max(100.*tolerance,axis.Magnitude()*1.0e-5));
    const auto middle=curve.Value(
        (curve.FirstParameter()+curve.LastParameter())/2.);
    BRepClass3d_SolidClassifier wedge(material,
        middle.Translated(bisector*probe),tolerance);
    BRepClass3d_SolidClassifier opposite(material,
        middle.Translated(-bisector*probe),tolerance);
    const bool removes=wedge.State()==TopAbs_IN&&
        opposite.State()==TopAbs_OUT;
    const bool adds=wedge.State()==TopAbs_OUT&&
        opposite.State()==TopAbs_IN;
    if(!removes&&!adds)
        throw std::invalid_argument(
            "ZIMA Chamfer could not classify the material wedge");

    const auto first_tangent=origin.Translated(
        distance*gp_Vec(first_direction));
    const auto second_tangent=origin.Translated(
        distance*gp_Vec(second_direction));
    const auto treatment_edge=
        BRepBuilderAPI_MakeEdge(first_tangent,second_tangent).Edge();
    BRepBuilderAPI_MakeWire profile;
    profile.Add(treatment_edge);
    profile.Add(BRepBuilderAPI_MakeEdge(second_tangent,origin).Edge());
    profile.Add(BRepBuilderAPI_MakeEdge(origin,first_tangent).Edge());
    if(!profile.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct its route profile");
    BRepBuilderAPI_MakeWire spine;
    for(const auto& member:route.edges)spine.Add(member);
    if(!spine.IsDone())
        throw std::runtime_error(
            "ZIMA Chamfer could not construct one route wire");
    BRepOffsetAPI_MakePipeShell sweep(spine.Wire());
    sweep.SetMode(route.planar_binormal);
    sweep.SetTolerance(tolerance,tolerance,1.0e-6);
    sweep.Add(profile.Wire(),route.start_vertex,false,false);
    sweep.Build();
    if(!sweep.IsDone()||!sweep.MakeSolid()||sweep.Shape().IsNull()||
       !BRepCheck_Analyzer(sweep.Shape()).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer could not sweep one valid route tool");
    std::vector<TopoDS_Face> treatment_faces;
    for(const auto& generated:sweep.Generated(treatment_edge))
        if(generated.ShapeType()==TopAbs_FACE&&
           std::ranges::none_of(treatment_faces,[&](const auto& current) {
               return current.IsSame(generated);
           }))treatment_faces.push_back(TopoDS::Face(generated));
    if(treatment_faces.empty())
        throw std::runtime_error(
            "ZIMA Chamfer route sweep omitted its treatment face");
    return {sweep.Shape(),std::move(treatment_faces),adds,material,
        first_face,second_face};
}

} // namespace

OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
        const TopoDS_Shape& input,
        std::span<const OcctPlanarEdgeChamferSelection> selections,
        OcctPlanarEdgeChamferMode mode,double distance_a,
        double distance_b_or_angle,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        std::span<const OcctPlanarEdgeSupport> supports,
        OcctPlanarEdgeEndMode end_mode) {
    if(input.IsNull()||selections.empty())
        throw std::invalid_argument(
            "ZIMA Chamfer requires a Body and selected edges");
    if(!std::isfinite(distance_a)||distance_a<=0.||
       (mode==OcctPlanarEdgeChamferMode::TwoDistances&&
        (!std::isfinite(distance_b_or_angle)||
         distance_b_or_angle<=0.))||
       (mode==OcctPlanarEdgeChamferMode::DistanceAngle&&
        (!std::isfinite(distance_b_or_angle)||
         distance_b_or_angle<=0.||
         distance_b_or_angle>=std::numbers::pi))||
       !std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "ZIMA Chamfer distances, angle and tolerance are invalid");
    if(!end_faces.empty()&&end_faces.size()!=selections.size())
        throw std::invalid_argument(
            "ZIMA Chamfer end faces must be parallel to selected edges");
    if(!supports.empty()&&supports.size()!=selections.size())
        throw std::invalid_argument(
            "ZIMA Chamfer supports must be parallel to selected edges");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces,edge_solids;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,edge_faces);
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_SOLID,edge_solids);
    TopTools_IndexedMapOfShape requested_edges;
    std::vector<SelectedNode> selected_nodes;
    std::vector<std::array<TopoDS_Face,2>> selection_faces;
    std::vector<TopoDS_Solid> selection_materials;
    selection_faces.reserve(selections.size());
    selection_materials.reserve(selections.size());
    for(std::size_t selection_index=0;
            selection_index<selections.size();++selection_index) {
        const auto& selection=selections[selection_index];
        const auto& edge=selection.edge;
        if(requested_edges.Contains(edge))
            throw std::invalid_argument(
                "ZIMA Chamfer received the same edge more than once");
        requested_edges.Add(edge);
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        for(int index=1;index<=vertices.Extent();++index) {
            const auto vertex=TopoDS::Vertex(vertices.FindKey(index));
            const auto found=std::ranges::find_if(selected_nodes,
                [&](const auto& node) {
                    return node.vertex.IsSame(vertex);
                });
            if(found==selected_nodes.end())
                selected_nodes.push_back({vertex,{selection_index}});
            else
                found->tools.push_back(selection_index);
        }
        std::vector<TopoDS_Face> faces;
        std::vector<TopoDS_Solid> solids;
        const bool exact_source=!supports.empty()&&
            !supports[selection_index].material.IsNull();
        if(exact_source) {
            const auto& support=supports[selection_index];
            if(support.first.IsNull()||support.second.IsNull()||
               support.first.IsSame(support.second))
                throw std::invalid_argument(
                    "ZIMA Chamfer original support is invalid");
            TopTools_IndexedMapOfShape first_edges,second_edges,
                material_edges;
            TopExp::MapShapes(support.first,TopAbs_EDGE,first_edges);
            TopExp::MapShapes(support.second,TopAbs_EDGE,second_edges);
            TopExp::MapShapes(support.material,TopAbs_EDGE,material_edges);
            if(!first_edges.Contains(edge)||!second_edges.Contains(edge)||
               !material_edges.Contains(edge))
                throw std::invalid_argument(
                    "ZIMA Chamfer original support does not contain its edge");
            faces={support.first,support.second};
            solids={support.material};
        } else {
            const int face_index=edge_faces.FindIndex(edge);
            const int solid_index=edge_solids.FindIndex(edge);
            if(!face_index||!solid_index)
                throw std::invalid_argument(
                    "ZIMA Chamfer edge is absent from the input Body");
            for(const auto& value:edge_faces.FindFromIndex(face_index)) {
                const auto face=TopoDS::Face(value);
                if(std::ranges::none_of(faces,[&](const auto& current) {
                        return current.IsSame(face);}))faces.push_back(face);
            }
            for(const auto& value:edge_solids.FindFromIndex(solid_index)) {
                const auto solid=TopoDS::Solid(value);
                if(std::ranges::none_of(solids,[&](const auto& current) {
                        return current.IsSame(solid);}))solids.push_back(solid);
            }
        }
        if(faces.size()!=2||solids.size()!=1)
            throw std::invalid_argument(
                "ZIMA Chamfer requires one manifold solid edge");
        if(mode!=OcctPlanarEdgeChamferMode::EqualDistance&&
                (selection.distance_a_face.IsNull()||
                 (!selection.distance_a_face.IsSame(faces[0])&&
                  !selection.distance_a_face.IsSame(faces[1]))))
            throw std::invalid_argument(
                selection.distance_a_face.IsNull()
                    ?"ZIMA Chamfer requires the stable A support face"
                    :"ZIMA Chamfer A support is not adjacent to its edge");
        selection_faces.push_back({faces[0],faces[1]});
        selection_materials.push_back(solids.front());
    }

    if(mode!=OcctPlanarEdgeChamferMode::EqualDistance) {
        // A/B is authored once on the first member, then transported through
        // the one connected route.  At a turn the common support keeps its
        // current role and the new support inherits the other role.  This
        // avoids reinterpreting A/B from independent face-name sorting on
        // every segment.
        if(selections[0].distance_a_face.IsSame(selection_faces[0][1]))
            std::swap(selection_faces[0][0],selection_faces[0][1]);
        std::vector<bool> oriented(selections.size());
        oriented[0]=true;
        std::queue<std::size_t> pending;
        pending.push(0);
        while(!pending.empty()) {
            const auto current=pending.front();
            pending.pop();
            for(const auto& node:selected_nodes) {
                if(std::ranges::find(node.tools,current)==node.tools.end())
                    continue;
                for(const auto neighbor:node.tools) {
                    if(neighbor==current)continue;
                    int current_common=-1,neighbor_common=-1,common_count{};
                    for(int first=0;first<2;++first)
                        for(int second=0;second<2;++second)
                            if(selection_faces[current][first].IsSame(
                                    selection_faces[neighbor][second])) {
                                current_common=first;
                                neighbor_common=second;
                                ++common_count;
                            }
                    if(common_count==0)
                        throw std::invalid_argument(
                            "A x B and A + angle Chamfer route has no continuous support side");
                    int neighbor_a{};
                    if(common_count==2) {
                        neighbor_a=selection_faces[neighbor][0].IsSame(
                            selection_faces[current][0])?0:1;
                    } else if(common_count==1) {
                        neighbor_a=current_common==0
                            ?neighbor_common:1-neighbor_common;
                    } else {
                        throw std::invalid_argument(
                            "A x B and A + angle Chamfer route has ambiguous support sides");
                    }
                    const auto intended_a=selection_faces[neighbor][neighbor_a];
                    if(oriented[neighbor]) {
                        if(!selection_faces[neighbor][0].IsSame(intended_a))
                            throw std::invalid_argument(
                                "A x B and A + angle Chamfer route reverses A/B sides");
                        continue;
                    }
                    if(neighbor_a==1)
                        std::swap(selection_faces[neighbor][0],
                            selection_faces[neighbor][1]);
                    oriented[neighbor]=true;
                    pending.push(neighbor);
                }
            }
        }
        if(std::ranges::any_of(oriented,[](bool value){return !value;}))
            throw std::invalid_argument(
                "A x B and A + angle Chamfer require one connected route");
    }

    const auto continuous_route=find_continuous_route(selections,
        selected_nodes,selection_faces,selection_materials,mode,end_faces,
        end_mode,tolerance);
    const auto continuous_tool=continuous_route
        ?std::optional<Tool>(make_continuous_route_tool(*continuous_route,
            selections,selection_faces,selection_materials,distance_a,
            tolerance))
        :std::optional<Tool>{};

    // Equal-distance Chamfer branches use the same swept triangular profile.
    // At a selected two-branch node, extend both profiles through the node and
    // cut them by their equal-distance bisector plane. This makes the profile
    // end itself own the trimming geometry, including for additive Chamfers,
    // instead of relying on a later overlapping connector to hide an open end.
    std::vector<std::array<TopoDS_Face,2>> node_end_faces(
        selections.size());
    if(!continuous_tool&&
            mode==OcctPlanarEdgeChamferMode::EqualDistance) {
        const auto assign_node_face=[&](std::size_t selection_index,
                const TopoDS_Vertex& vertex,const TopoDS_Face& face) {
            const BRepAdaptor_Curve curve(selections[selection_index].edge);
            const auto point=BRep_Tool::Pnt(vertex);
            const auto first=curve.Value(curve.FirstParameter());
            const auto last=curve.Value(curve.LastParameter());
            auto& target=point.Distance(first)<=point.Distance(last)
                ?node_end_faces[selection_index][0]
                :node_end_faces[selection_index][1];
            if(!target.IsNull())
                throw std::invalid_argument(
                    "ZIMA Chamfer branch has more than one node trim");
            target=face;
        };
        for(const auto& node:selected_nodes) {
            if(node.tools.size()!=2)continue;
            const auto first_index=node.tools[0];
            const auto second_index=node.tools[1];
            if(!selection_materials[first_index].IsSame(
                    selection_materials[second_index]))continue;
            const auto first_direction=outgoing_tangent(
                selections[first_index].edge,node.vertex,tolerance);
            const auto second_direction=outgoing_tangent(
                selections[second_index].edge,node.vertex,tolerance);
            if(first_direction.IsParallel(second_direction,
                    std::max(1.0e-9,tolerance)))continue;
            gp_Vec normal(first_direction);
            normal-=gp_Vec(second_direction);
            if(normal.SquareMagnitude()<=1.0e-18)continue;
            BRepBuilderAPI_MakeFace miter(gp_Pln(
                BRep_Tool::Pnt(node.vertex),gp_Dir(normal)));
            if(!miter.IsDone()||miter.Face().IsNull())continue;
            assign_node_face(first_index,node.vertex,miter.Face());
            assign_node_face(second_index,node.vertex,miter.Face());
        }
    }

    std::vector<Tool> tools;
    tools.reserve(selections.size());
    for(std::size_t selection_index=0;
            selection_index<selections.size();++selection_index) {
        if(continuous_tool) {
            tools.push_back(*continuous_tool);
            continue;
        }
        auto& faces=selection_faces[selection_index];
        TopoDS_Vertex first_vertex,last_vertex;
        TopExp::Vertices(selections[selection_index].edge,
            first_vertex,last_vertex,true);
        const auto is_route_end=[&](const TopoDS_Vertex& vertex) {
            const auto node=std::ranges::find_if(selected_nodes,
                [&](const auto& candidate) {
                    return !vertex.IsNull()&&
                        candidate.vertex.IsSame(vertex);
                });
            return node==selected_nodes.end()||node->tools.size()==1;
        };
        auto tool=make_tool(selections[selection_index].edge,
            faces[0],faces[1],mode,distance_a,
            distance_b_or_angle,tolerance,
            selection_materials[selection_index],
            end_faces.empty()?OcctPlanarEdgeEndFaces{}:
                end_faces[selection_index],end_mode,
            {is_route_end(first_vertex),is_route_end(last_vertex)},
            node_end_faces[selection_index]);
        tools.push_back(std::move(tool));
    }

    // One persisted asymmetric route may turn at a two-edge node. A/B is not
    // chosen again after that turn: the two segments must share the same
    // support in the same role. This makes the distance pair (or A + angle)
    // continuous while rejecting a side swap, a branch or an ambiguous node.
    // Symmetric A x A additionally supports the deterministic inner/outer
    // two- and three-branch base cases below.
    // The signed tools already encode inner (FUSE) versus outer (CUT), so a
    // mixed-sense node is rejected instead of depending on click order.
    const double angular_tolerance=std::max(1.0e-9,tolerance);
    std::vector<NodeConnector> node_connectors;
    for(const auto& node:selected_nodes) {
        if(node.tools.size()<2)continue;
        if(node.tools.size()>3)
            throw std::invalid_argument(
                "ZIMA Chamfer supports only two- or three-branch nodes");
        const auto first_direction=outgoing_tangent(
            selections[node.tools.front()].edge,node.vertex,tolerance);
        const bool one_line=node.tools.size()==2&&
            first_direction.IsParallel(outgoing_tangent(
                selections[node.tools.back()].edge,node.vertex,tolerance),
                angular_tolerance);
        if(mode!=OcctPlanarEdgeChamferMode::EqualDistance) {
            if(node.tools.size()!=2)
                throw std::invalid_argument(
                    "A x B and A + angle Chamfer require one continuous line");
            const auto first=node.tools.front();
            const auto second=node.tools.back();
            if(!tools[first].material.IsSame(tools[second].material)||
               tools[first].adds_material!=tools[second].adds_material)
                throw std::invalid_argument(
                    "A x B and A + angle Chamfer route changes material side");
            const bool a_continues=tools[first].first_support.IsSame(
                tools[second].first_support);
            const bool b_continues=tools[first].second_support.IsSame(
                tools[second].second_support);
            if(one_line) {
                if(!a_continues||!b_continues)
                    throw std::invalid_argument(
                        "A x B and A + angle Chamfer route changes A/B sides");
            } else if(a_continues==b_continues) {
                // A proper turn has exactly one common support. If neither or
                // both roles match, the next segment cannot inherit A/B
                // unambiguously.
                throw std::invalid_argument(
                    "A x B and A + angle Chamfer route has ambiguous A/B sides");
            }
            continue;
        }
        if(one_line)continue;
        const bool adds_material=tools[node.tools.front()].adds_material;
        if(std::ranges::any_of(node.tools,[&](const auto index) {
                return tools[index].adds_material!=adds_material;
            }))
            throw std::invalid_argument(
                "ZIMA Chamfer node mixes inner and outer branches");
        if(node.tools.size()==3) {
            std::array<gp_Dir,3> branches{
                outgoing_tangent(selections[node.tools[0]].edge,
                    node.vertex,tolerance),
                outgoing_tangent(selections[node.tools[1]].edge,
                    node.vertex,tolerance),
                outgoing_tangent(selections[node.tools[2]].edge,
                    node.vertex,tolerance)};
            node_connectors.push_back({make_three_branch_connector(
                node.vertex,branches,distance_a,tolerance),adds_material});
        } else {
            const auto& material=tools[node.tools.front()].material;
            if(std::ranges::any_of(node.tools,[&](const auto index) {
                    return !tools[index].material.IsSame(material);
                }))
                throw std::invalid_argument(
                    "ZIMA Chamfer node spans more than one material solid");
            std::vector<TopoDS_Edge> unselected;
            for(TopExp_Explorer item(material,TopAbs_EDGE);
                    item.More();item.Next()) {
                const auto edge=TopoDS::Edge(item.Current());
                TopTools_IndexedMapOfShape vertices;
                TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
                if(!vertices.Contains(node.vertex)||
                        std::ranges::any_of(node.tools,
                            [&](const auto index) {
                                return selections[index].edge.IsSame(edge);
                            }))continue;
                if(std::ranges::none_of(unselected,
                        [&](const auto& current) {
                            return current.IsSame(edge);
                        }))unselected.push_back(edge);
            }
            if(unselected.size()==1) {
                const std::array branches{first_direction,
                    outgoing_tangent(
                        selections[node.tools.back()].edge,node.vertex,
                        tolerance),
                    outgoing_tangent(unselected.front(),node.vertex,
                        tolerance)};
                const int occupied=material_octants(material,
                    BRep_Tool::Pnt(node.vertex),branches,
                    std::max(100.*tolerance,distance_a*.05),tolerance);
                if((adds_material&&occupied==5)||
                        (!adds_material&&occupied==3))
                    node_connectors.push_back({make_step_connector(
                        node.vertex,branches[0],branches[1],branches[2],
                        distance_a,tolerance),adds_material});
            }
        }
    }

    OcctPlanarEdgeChamferResult result;
    result.treatment_domains.reserve(tools.size());
    for(const auto& tool:tools)result.treatment_domains.push_back(tool.solid);
    TopoDS_Shape current=input;
    const auto apply=[&](auto& operation,std::size_t first,
            std::size_t last) {
        NCollection_List<TopoDS_Shape> arguments,boolean_tools;
        arguments.Append(current);
        for(std::size_t index=first;index<last;++index) {
            const bool duplicate=std::ranges::any_of(
                std::views::iota(first,index),[&](const auto previous) {
                    return tools[previous].solid.IsSame(tools[index].solid);
                });
            if(!duplicate)boolean_tools.Append(tools[index].solid);
        }
        operation.SetArguments(arguments);operation.SetTools(boolean_tools);
        operation.SetNonDestructive(true);operation.SetRunParallel(false);
        operation.SetToFillHistory(true);operation.SetFuzzyValue(tolerance);
        operation.Build();
        if(!operation.IsDone()||operation.Shape().IsNull())
            throw std::runtime_error("ZIMA Chamfer Boolean failed");
        auto stage_history=operation.History();
        if(!stage_history)
            throw std::runtime_error(
                "ZIMA Chamfer Boolean omitted topology history");
        if(result.history)result.history->Merge(stage_history);
        else result.history=std::move(stage_history);
        current=operation.Shape();
    };
    // Preserve authored edge order while batching consecutive tools with the
    // same material sense. This admits useful mixed outer/inner selections
    // without weakening final Body validation or reference history.
    for(std::size_t first=0;first<tools.size();) {
        std::size_t last=first+1;
        while(last<tools.size()&&
              tools[last].adds_material==tools[first].adds_material)
            ++last;
        if(tools[first].adds_material) {
            BRepAlgoAPI_Fuse fuse;apply(fuse,first,last);
        } else {
            BRepAlgoAPI_Cut cut;apply(cut,first,last);
        }
        first=last;
    }
    // Three mutually orthogonal bands leave one tetrahedral gap at their
    // common node. Its vertices are the pairwise A-offset points and the
    // intersection of all three chamfer planes; its exact volume is A^3/12.
    // A two-branch step leaves the corresponding oblique square pyramid,
    // whose exact volume is A^3/3. Apply either connector after the bands so
    // an inner connector is already attached to fused material and an outer
    // connector cuts only the remaining node gap.
    for(const auto& connector:node_connectors) {
        NCollection_List<TopoDS_Shape> arguments,boolean_tools;
        arguments.Append(current);
        boolean_tools.Append(connector.solid);
        if(connector.adds_material) {
            BRepAlgoAPI_Fuse fuse;
            fuse.SetArguments(arguments);fuse.SetTools(boolean_tools);
            fuse.SetNonDestructive(true);fuse.SetRunParallel(false);
            fuse.SetToFillHistory(true);fuse.SetFuzzyValue(tolerance);
            fuse.Build();
            if(!fuse.IsDone()||fuse.Shape().IsNull()||!fuse.History())
                throw std::runtime_error(
                    "ZIMA Chamfer node Boolean failed");
            result.history->Merge(fuse.History());
            current=fuse.Shape();
        } else {
            BRepAlgoAPI_Cut cut;
            cut.SetArguments(arguments);cut.SetTools(boolean_tools);
            cut.SetNonDestructive(true);cut.SetRunParallel(false);
            cut.SetToFillHistory(true);cut.SetFuzzyValue(tolerance);
            cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull()||!cut.History())
                throw std::runtime_error(
                    "ZIMA Chamfer node Boolean failed");
            result.history->Merge(cut.History());
            current=cut.Shape();
        }
    }
    // The two support sides of the exact triangular tool lie on the original
    // planes. OCCT may retain their end sections as coplanar Boolean seams.
    // Remove only those redundant new seams while protecting surviving input
    // boundaries and their non-coplanar descendants.
    ShapeUpgrade_UnifySameDomain cleanup(current,true,true,false);
    cleanup.SetLinearTolerance(tolerance);
    cleanup.SetAngularTolerance(1.0e-9);
    TopTools_IndexedMapOfShape current_edges,input_edges;
    TopExp::MapShapes(current,TopAbs_EDGE,current_edges);
    TopExp::MapShapes(input,TopAbs_EDGE,input_edges);
    TopTools_IndexedDataMapOfShapeListOfShape current_edge_faces;
    TopExp::MapShapesAndAncestors(current,TopAbs_EDGE,TopAbs_FACE,
        current_edge_faces);
    const auto redundant_coplanar_seam=[&](const TopoDS_Shape& edge) {
        const int edge_index=current_edge_faces.FindIndex(edge);
        if(!edge_index)return false;
        const auto& adjacent=current_edge_faces.FindFromIndex(edge_index);
        if(adjacent.Extent()!=2)return false;
        auto iterator=adjacent.cbegin();
        const BRepAdaptor_Surface first_surface(
            TopoDS::Face(*iterator),true);
        ++iterator;
        const BRepAdaptor_Surface second_surface(
            TopoDS::Face(*iterator),true);
        return first_surface.GetType()==GeomAbs_Plane&&
            second_surface.GetType()==GeomAbs_Plane&&
            first_surface.Plane().Axis().Direction().IsParallel(
                second_surface.Plane().Axis().Direction(),1.0e-9)&&
            first_surface.Plane().Distance(
                second_surface.Plane().Location())<=tolerance;
    };
    // Keep all current non-seam boundaries, including unchanged disconnected
    // split descendants that OCCT does not necessarily list in the Boolean
    // Modified/Generated history.
    for(int index=1;index<=current_edges.Extent();++index) {
        const auto& edge=current_edges.FindKey(index);
        if(!redundant_coplanar_seam(edge)) {
            cleanup.KeepShape(edge);
            continue;
        }
        const bool existing_boundary=std::ranges::any_of(
            std::views::iota(1,input_edges.Extent()+1),
            [&](int input_index) {
                return BRepTools::Compare(TopoDS::Edge(edge),
                    TopoDS::Edge(input_edges.FindKey(input_index)));
            });
        if(existing_boundary)cleanup.KeepShape(edge);
    }
    for(int index=1;index<=input_edges.Extent();++index) {
        const auto& source=input_edges.FindKey(index);
        if(current_edges.Contains(source))cleanup.KeepShape(source);
        for(const auto* descendants:{&result.history->Modified(source),
                &result.history->Generated(source)})
            for(const auto& descendant:*descendants)
                if(descendant.ShapeType()==TopAbs_EDGE&&
                   current_edges.Contains(descendant)&&
                   !redundant_coplanar_seam(descendant))
                    cleanup.KeepShape(descendant);
    }
    cleanup.Build();
    if(cleanup.Shape().IsNull())
        throw std::runtime_error(
            "ZIMA Chamfer could not remove auxiliary result seams");
    if(const auto cleanup_history=cleanup.History();
            !cleanup_history.IsNull())
        result.history->Merge(cleanup_history);
    current=cleanup.Shape();
    result.shape=current;
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,result_faces);
    result.chamfer_faces.resize(tools.size());
    for(std::size_t index=0;index<tools.size();++index) {
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto face=TopoDS::Face(shape);
            if(std::ranges::none_of(result.chamfer_faces[index],
                    [&](const auto& current_face) {
                        return current_face.IsSame(face);}))
                result.chamfer_faces[index].push_back(face);
        };
        for(const auto& treatment_face:tools[index].treatment_faces) {
            append(treatment_face);
            for(const auto* descendants:{
                    &result.history->Modified(treatment_face),
                    &result.history->Generated(treatment_face)})
                for(const auto& descendant:*descendants)append(descendant);
        }
        if(result.chamfer_faces[index].empty())
            throw std::runtime_error(
                "ZIMA Chamfer lost its result-face ancestry");
    }
    if(!BRepCheck_Analyzer(result.shape,true,true,false).IsValid())
        throw std::runtime_error(
            "ZIMA Chamfer produced an invalid Body");
    result.source_edges.reserve(selections.size());
    for(const auto& selection:selections)
        result.source_edges.push_back(selection.edge);
    result.generated_from_edges.resize(selections.size());
    for(std::size_t index=0;index<selections.size();++index) {
        auto& generated=result.generated_from_edges[index];
        for(const auto& shape:result.history->Generated(
                selections[index].edge))
            generated.Append(shape);
        for(const auto& face:result.chamfer_faces[index])
            if(std::ranges::none_of(generated,[&](const auto& shape) {
                    return shape.IsSame(face);}))generated.Append(face);
    }
    return result;
}

OcctPlanarEdgeChamferResult occt_planar_edge_chamfer(
        const TopoDS_Shape& input,std::span<const TopoDS_Edge> edges,
        double distance,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        std::span<const OcctPlanarEdgeSupport> supports,
        OcctPlanarEdgeEndMode end_mode) {
    std::vector<OcctPlanarEdgeChamferSelection> selections;
    selections.reserve(edges.size());
    for(const auto& edge:edges)selections.push_back({edge,{}});
    return occt_planar_edge_chamfer(input,selections,
        OcctPlanarEdgeChamferMode::EqualDistance,distance,distance,
        tolerance,end_faces,supports,end_mode);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeChamferResult::Modified(
        const TopoDS_Shape& source) const {
    return history->Modified(source);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeChamferResult::Generated(
        const TopoDS_Shape& source) const {
    for(std::size_t index=0;index<source_edges.size();++index)
        if(source_edges[index].IsSame(source))
            return generated_from_edges[index];
    return history->Generated(source);
}

bool OcctPlanarEdgeChamferResult::IsDeleted(
        const TopoDS_Shape& source) const {
    return history->IsRemoved(source);
}

} // namespace zima::kernel
