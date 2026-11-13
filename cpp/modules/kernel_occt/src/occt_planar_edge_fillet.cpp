#include <zima/kernel/occt_planar_edge_fillet.hpp>
#include "occt_edge_treatment_terminals.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <ChFi2d_FilletAlgo.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAPI_IntSS.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace zima::kernel {
namespace {

using edge_treatment_terminals::terminal_face_meets_support_at_endpoint;
using edge_treatment_terminals::local_curved_terminal_faces;
using edge_treatment_terminals::remote_terminal_faces_through_local_curve;
using edge_treatment_terminals::
    removal_profile_continues_past_curved_terminal;
using edge_treatment_terminals::
    route_crosses_curved_terminal;

using Clock=std::chrono::steady_clock;

struct Cutter {
    TopoDS_Shape solid;
    std::vector<TopoDS_Face> circular_faces;
    PlanarFilletAnalyticRemovalDomain analytic_domain;
    bool adds_material{};
    TopoDS_Wire midpoint_profile;
    TopoDS_Edge midpoint_circular_edge;
    std::vector<std::size_t> source_jobs;
};

struct CutterEndExtensions {
    double first{};
    double last{};
};

struct CutterJob {
    TopoDS_Edge edge;
    TopoDS_Face first_face;
    TopoDS_Face second_face;
    TopoDS_Solid material;
};

struct SharedEndpoint {
    TopoDS_Vertex vertex;
    std::vector<std::size_t> jobs;
};

struct NodeConnector {
    TopoDS_Shape solid;
    std::optional<gp_Pnt> sphere_center;
    std::vector<std::size_t> jobs;
    bool adds_material{};
};

struct EdgePath {
    EdgeFilletPathKind kind{EdgeFilletPathKind::Linear};
    double first_parameter{},last_parameter{},middle_parameter{},length{};
    gp_Pnt first,last,middle;
    gp_Dir first_tangent,last_tangent;
    gp_Ax1 revolution_axis;
    double path_radius{},sweep_angle{};
};

Vec3 vector(const gp_XYZ& value) {
    return {value.X(),value.Y(),value.Z()};
}

Vec3 point(const gp_Pnt& value) {
    return vector(value.XYZ());
}

gp_Dir node_outgoing_tangent(const TopoDS_Edge& edge,
        const TopoDS_Vertex& vertex,double tolerance) {
    const BRepAdaptor_Curve curve(edge);
    const auto vertex_point=BRep_Tool::Pnt(vertex);
    gp_Pnt first_point,last_point;
    gp_Vec first_tangent,last_tangent;
    curve.D1(curve.FirstParameter(),first_point,first_tangent);
    curve.D1(curve.LastParameter(),last_point,last_tangent);
    gp_Vec direction;
    if(vertex_point.Distance(first_point)<=
            vertex_point.Distance(last_point))
        direction=first_tangent;
    else
        direction=-last_tangent;
    if(direction.Magnitude()<=tolerance)
        throw std::invalid_argument(
            "ZIMA Fillet node has a degenerate branch");
    return gp_Dir(direction);
}

NodeConnector make_fillet_corner_connector(
        const TopoDS_Vertex& vertex,
        std::array<gp_Dir,3> branches,double radius,double tolerance,
        std::vector<std::size_t> jobs,bool adds_material) {
    for(std::size_t first=0;first<branches.size();++first)
        for(std::size_t second=first+1;second<branches.size();++second)
            if(std::abs(branches[first].Dot(branches[second]))>1.0e-7)
                throw std::invalid_argument(
                    "ZIMA Fillet requires orthogonal three-branch nodes");
    // Reorder only the local axes into a right-handed frame. The three
    // physical outgoing directions and their persisted jobs remain intact.
    if(gp_Vec(branches[0]).Crossed(gp_Vec(branches[1])).Dot(
            gp_Vec(branches[2]))<0.)
        std::swap(branches[1],branches[2]);
    const auto origin=BRep_Tool::Pnt(vertex);
    const gp_Ax2 frame(origin,branches[2],branches[0]);
    BRepPrimAPI_MakeBox corner(frame,radius,radius,radius);
    const auto center=origin.Translated(radius*(gp_Vec(branches[0])+
        gp_Vec(branches[1])+gp_Vec(branches[2])));
    BRepPrimAPI_MakeSphere sphere(center,radius);
    BRepAlgoAPI_Cut connector(corner.Shape(),sphere.Shape());
    connector.SetNonDestructive(true);
    connector.SetRunParallel(false);
    connector.SetFuzzyValue(tolerance);
    connector.Build();
    if(!connector.IsDone()||connector.Shape().IsNull()||
            !BRepCheck_Analyzer(connector.Shape()).IsValid())
        throw std::runtime_error(
            "ZIMA Fillet could not construct a valid node connector");
    return {connector.Shape(),center,std::move(jobs),adds_material};
}

EdgePath edge_path(const TopoDS_Edge& edge,double tolerance) {
    const BRepAdaptor_Curve curve(edge);
    EdgePath result;
    result.first_parameter=curve.FirstParameter();
    result.last_parameter=curve.LastParameter();
    if(!std::isfinite(result.first_parameter)||
       !std::isfinite(result.last_parameter)||
       result.last_parameter<=result.first_parameter)
        throw std::invalid_argument(
            "Planar edge Fillet requires a finite edge");
    gp_Vec first_derivative,last_derivative;
    curve.D1(result.first_parameter,result.first,first_derivative);
    curve.D1(result.last_parameter,result.last,last_derivative);
    const double coordinate_scale=std::max({1.,std::abs(result.first.X()),
        std::abs(result.first.Y()),std::abs(result.first.Z()),
        std::abs(result.last.X()),std::abs(result.last.Y()),
        std::abs(result.last.Z())});
    const double geometry_tolerance=std::max({tolerance,
        BRep_Tool::Tolerance(edge),
        64.*std::numeric_limits<double>::epsilon()*coordinate_scale});
    if(curve.GetType()==GeomAbs_Circle) {
        const auto circle=curve.Circle();
        result.kind=EdgeFilletPathKind::Circular;
        result.path_radius=circle.Radius();
        result.sweep_angle=result.last_parameter-result.first_parameter;
        if(!(result.path_radius>geometry_tolerance)||
           !(result.sweep_angle>0.)||
           result.sweep_angle>2.*std::numbers::pi+1.0e-10)
            throw std::invalid_argument(
                "Planar edge Fillet requires a finite edge");
        auto axis=circle.Axis().Direction();
        const gp_Vec radial(circle.Location(),result.first);
        if(gp_Vec(axis).Crossed(radial).Dot(first_derivative)<0.)
            axis.Reverse();
        result.revolution_axis=gp_Ax1(circle.Location(),axis);
        result.length=result.path_radius*result.sweep_angle;
        result.middle_parameter=(result.first_parameter+
            result.last_parameter)/2.;
        result.middle=curve.Value(result.middle_parameter);
    } else {
        const gp_Vec chord(result.first,result.last);
        result.length=chord.Magnitude();
        if(!(result.length>geometry_tolerance))
            throw std::invalid_argument(
                "Planar edge Fillet edge is too short");
        const gp_Dir direction(chord);
        const gp_Lin line(result.first,direction);
        for(const double fraction:{.25,.5,.75}) {
            const auto parameter=result.first_parameter+fraction*
                (result.last_parameter-result.first_parameter);
            gp_Pnt sample;gp_Vec derivative;
            curve.D1(parameter,sample,derivative);
            if(line.Distance(sample)>geometry_tolerance||
               derivative.SquareMagnitude()<=1.0e-18||
               gp_Dir(derivative).Dot(direction)<1.-1.0e-10)
                throw std::invalid_argument(
                    "Planar edge Fillet requires a straight or circular edge");
        }
        const double curve_length=GCPnts_AbscissaPoint::Length(curve,
            result.first_parameter,result.last_parameter,geometry_tolerance);
        if(!std::isfinite(curve_length)||
           std::abs(curve_length-result.length)>geometry_tolerance)
            throw std::invalid_argument(
                "Planar edge Fillet requires a straight or circular edge");
        GCPnts_AbscissaPoint middle(geometry_tolerance,curve,
            result.length/2.,result.first_parameter);
        if(!middle.IsDone())
            throw std::invalid_argument(
                "Planar edge Fillet could not locate the path midpoint");
        result.middle_parameter=middle.Parameter();
        result.middle=curve.Value(result.middle_parameter);
        result.first_tangent=direction;
        result.last_tangent=direction;
        return result;
    }
    if(first_derivative.SquareMagnitude()<=1.0e-18||
       last_derivative.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet support direction is degenerate");
    result.first_tangent=gp_Dir(first_derivative);
    result.last_tangent=gp_Dir(last_derivative);
    return result;
}

gp_Trsf path_rotation(const EdgePath& path,double angle) {
    gp_Trsf result;
    result.SetRotation(path.revolution_axis,angle);
    return result;
}

gp_Pnt transported(const EdgePath& path,const gp_Pnt& value,
        double distance) {
    if(path.kind==EdgeFilletPathKind::Linear)
        return value.Translated(gp_Vec(path.first_tangent)*distance);
    return value.Transformed(path_rotation(path,distance/path.path_radius));
}

gp_Vec transported(const EdgePath& path,const gp_Vec& value,
        double distance) {
    if(path.kind==EdgeFilletPathKind::Linear)return value;
    return value.Transformed(path_rotation(path,distance/path.path_radius));
}

struct FilletProfile {
    TopoDS_Wire wire;
    TopoDS_Edge circular_edge;
};

FilletProfile fillet_profile(const gp_Pnt& origin,
        const gp_Dir& first_inward,const gp_Dir& second_inward,
        gp_Vec bisector,double tangent_distance,double center_distance,
        double radius) {
    bisector.Normalize();
    const gp_Pnt first_tangent=origin.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt second_tangent=origin.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt center=origin.Translated(bisector*center_distance);
    const gp_Pnt arc_middle=center.Translated(-bisector*radius);
    const auto arc=GC_MakeArcOfCircle(first_tangent,arc_middle,
        second_tangent).Value();
    if(arc.IsNull())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its circular boundary");
    FilletProfile result;
    result.circular_edge=BRepBuilderAPI_MakeEdge(arc).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(result.circular_edge);
    wire.Add(BRepBuilderAPI_MakeEdge(second_tangent,origin).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(origin,first_tangent).Edge());
    if(!wire.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its profile wire");
    result.wire=wire.Wire();
    return result;
}

gp_Dir inward_direction(const TopoDS_Edge& edge,
        const TopoDS_Face& face,const BRepAdaptor_Curve& curve,
        double parameter,const gp_Dir& tangent,double tolerance) {
    std::optional<TopAbs_Orientation> orientation;
    for(TopExp_Explorer item(face,TopAbs_EDGE);item.More();item.Next())
        if(item.Current().IsSame(edge)) {
            const auto current=item.Current().Orientation();
            if(current!=TopAbs_FORWARD&&current!=TopAbs_REVERSED)
                throw std::invalid_argument(
                    "Planar edge Fillet support has an invalid edge orientation");
            if(orientation&&*orientation!=current)
                throw std::invalid_argument(
                    "Planar edge Fillet support uses its edge more than once");
            orientation=current;
        }
    if(!orientation)
        throw std::invalid_argument(
            "Planar edge Fillet support does not contain its edge");
    double first{},last{};
    const auto pcurve=BRep_Tool::CurveOnSurface(edge,face,first,last);
    if(pcurve.IsNull()||parameter<first-tolerance||
       parameter>last+tolerance)
        throw std::invalid_argument(
            "Planar edge Fillet support has no exact edge parameterization");
    const auto uv=pcurve->Value(parameter);
    BRepAdaptor_Surface support(face);
    gp_Pnt surface_point,curve_point;
    gp_Vec du,dv;
    support.D1(uv.X(),uv.Y(),surface_point,du,dv);
    curve.D0(parameter,curve_point);
    if(surface_point.Distance(curve_point)>std::max(tolerance,
            BRep_Tool::Tolerance(edge))||
       du.Crossed(dv).SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet support has no exact edge parameterization");
    gp_Vec normal;
    const BRepAdaptor_Surface analytic_support(face,true);
    if(analytic_support.GetType()==GeomAbs_Plane) {
        auto plane_normal=analytic_support.Plane().Axis().Direction();
        if(!analytic_support.Plane().Position().Direct())
            plane_normal.Reverse();
        if(face.Orientation()==TopAbs_REVERSED)plane_normal.Reverse();
        normal=gp_Vec(plane_normal);
    } else {
        normal=du.Crossed(dv);
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    }
    auto direction=normal.Crossed(gp_Vec(tangent));
    if(*orientation==TopAbs_REVERSED)direction.Reverse();
    if(direction.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet support direction is degenerate");
    return gp_Dir(direction);
}

bool lies_on_source_surface(const TopoDS_Face& candidate,
        const TopoDS_Face& source,double tolerance) {
    TopLoc_Location source_location,candidate_location;
    const auto source_surface=BRep_Tool::Surface(source,source_location);
    const auto candidate_surface=BRep_Tool::Surface(candidate,
        candidate_location);
    if(source_surface.IsNull()||candidate_surface.IsNull())return false;
    if(source_surface==candidate_surface&&
       source_location==candidate_location)return true;
    double first_u{},last_u{},first_v{},last_v{};
    BRepTools::UVBounds(candidate,first_u,last_u,first_v,last_v);
    if(!std::isfinite(first_u)||!std::isfinite(last_u)||
       !std::isfinite(first_v)||!std::isfinite(last_v))return false;
    const BRepAdaptor_Surface candidate_support(candidate,true);
    const auto to_source=source_location.Transformation().Inverted();
    for(const double fraction:{.2,.5,.8}) {
        gp_Pnt sample;
        candidate_support.D0(
            std::lerp(first_u,last_u,fraction),
            std::lerp(first_v,last_v,fraction),sample);
        sample.Transform(to_source);
        GeomAPI_ProjectPointOnSurf projection(sample,source_surface);
        if(projection.NbPoints()==0||
           projection.LowerDistance()>100.*tolerance)return false;
    }
    return true;
}

void clip_fillet_tool_with_halfspace(TopoDS_Shape& tool,
        std::vector<TopoDS_Face>& treatment_faces,
        const TopoDS_Solid& half_space,double tolerance) {
    BRepAlgoAPI_Common common(tool,half_space);
    common.SetNonDestructive(true);
    common.SetFuzzyValue(tolerance);
    common.Build();
    if(!common.IsDone()||common.Shape().IsNull()||
       !BRepCheck_Analyzer(common.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not trim its selected end face");
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
    if(clipped_faces.empty())
        for(int index=1;index<=result_faces.Extent();++index) {
            const auto candidate=TopoDS::Face(result_faces.FindKey(index));
            if(std::ranges::any_of(treatment_faces,
                    [&](const auto& source) {
                        return lies_on_source_surface(candidate,source,
                            tolerance);
                    }))append(candidate);
        }
    if(clipped_faces.empty())
        for(int index=1;index<=result_faces.Extent();++index) {
            const auto candidate=TopoDS::Face(result_faces.FindKey(index));
            const bool belongs=std::ranges::any_of(treatment_faces,
                [&](const auto& source) {
                    TopTools_IndexedMapOfShape vertices;
                    TopExp::MapShapes(candidate,TopAbs_VERTEX,vertices);
                    if(vertices.IsEmpty())return false;
                    for(int vertex=1;vertex<=vertices.Extent();++vertex) {
                        BRepClass_FaceClassifier classifier(source,
                            BRep_Tool::Pnt(TopoDS::Vertex(
                                vertices.FindKey(vertex))),10.*tolerance);
                        if(classifier.State()!=TopAbs_IN&&
                           classifier.State()!=TopAbs_ON)return false;
                    }
                    return true;
                });
            if(belongs)append(candidate);
        }
    if(clipped_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet trim lost its circular face");
    treatment_faces=std::move(clipped_faces);
    tool=common.Shape();
}

void clip_fillet_tool(TopoDS_Shape& tool,
        std::vector<TopoDS_Face>& treatment_faces,
        const TopoDS_Face& face,const gp_Pnt& inside,double tolerance) {
    if(face.IsNull())return;
    const BRepAdaptor_Surface end_support(face,true);
    const auto plane=end_support.Plane();
    Bnd_Box bounds;
    BRepBndLib::Add(tool,bounds);
    if(bounds.IsVoid()||bounds.IsWhole())
        throw std::runtime_error(
            "Planar edge Fillet could not bound its end trim");
    double x0{},y0{},z0{},x1{},y1{},z1{};
    bounds.Get(x0,y0,z0,x1,y1,z1);
    const gp_Pnt center((x0+x1)/2.,(y0+y1)/2.,(z0+z1)/2.);
    const double diagonal=gp_Pnt(x0,y0,z0).Distance(
        gp_Pnt(x1,y1,z1));
    if(!std::isfinite(diagonal))
        throw std::runtime_error(
            "Planar edge Fillet end trim has invalid bounds");
    auto retained_normal=plane.Axis().Direction();
    const double inside_side=gp_Vec(plane.Location(),inside).Dot(
        gp_Vec(retained_normal));
    if(std::abs(inside_side)<=tolerance)
        throw std::runtime_error(
            "Planar edge Fillet end trim has no retained side");
    if(inside_side<0.)retained_normal.Reverse();
    const auto plane_normal=plane.Axis().Direction();
    const double center_offset=gp_Vec(plane.Location(),center).Dot(
        gp_Vec(plane_normal));
    const auto projected=center.Translated(-center_offset*
        gp_Vec(plane_normal));
    const double extent=std::max(1.,plane.Distance(center)+diagonal+
        100.*tolerance);
    const gp_Ax2 centered_frame(projected,retained_normal,
        plane.Position().XDirection());
    const auto corner=projected.Translated(
        -extent*gp_Vec(centered_frame.XDirection())-
        extent*gp_Vec(centered_frame.YDirection()));
    const gp_Ax2 retained_frame(corner,retained_normal,
        centered_frame.XDirection());
    BRepPrimAPI_MakeBox retained_region(retained_frame,2.*extent,
        2.*extent,2.*extent);
    retained_region.Build();
    if(!retained_region.IsDone()||retained_region.Shape().IsNull())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its end trim");
    clip_fillet_tool_with_halfspace(tool,treatment_faces,
        retained_region.Solid(),tolerance);
}

void fuse_fillet_tools(TopoDS_Shape& tool,
        std::vector<TopoDS_Face>& treatment_faces,
        const TopoDS_Shape& addition,
        std::span<const TopoDS_Face> addition_faces,double tolerance) {
    BRepAlgoAPI_Fuse fuse(tool,addition);
    fuse.SetNonDestructive(true);
    fuse.SetFuzzyValue(tolerance);
    fuse.Build();
    if(!fuse.IsDone()||fuse.Shape().IsNull()||
       !BRepCheck_Analyzer(fuse.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not fuse its route extension");
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,result_faces);
    std::vector<TopoDS_Face> fused_faces;
    const auto append=[&](const TopoDS_Shape& shape) {
        if(shape.ShapeType()!=TopAbs_FACE||
           !result_faces.Contains(shape))return;
        const auto candidate=TopoDS::Face(shape);
        if(std::ranges::none_of(fused_faces,[&](const auto& current) {
                return current.IsSame(candidate);
            }))fused_faces.push_back(candidate);
    };
    const std::array face_sets{
        std::span<const TopoDS_Face>(treatment_faces),addition_faces};
    for(const auto faces:face_sets)
        for(const auto& face:faces) {
            append(face);
            for(const auto& descendant:fuse.Modified(face))
                append(descendant);
            for(const auto& descendant:fuse.Generated(face))
                append(descendant);
        }
    if(fused_faces.empty())for(int index=1;index<=result_faces.Extent();++index) {
        const auto candidate=TopoDS::Face(result_faces.FindKey(index));
        for(const auto faces:face_sets)
            if(std::ranges::any_of(faces,[&](const auto& source) {
                    return lies_on_source_surface(candidate,source,
                        tolerance);
                })) {append(candidate);break;}
    }
    if(fused_faces.empty())throw std::runtime_error(
        "Planar edge Fillet route fuse lost its circular face");
    tool=fuse.Shape();
    treatment_faces=std::move(fused_faces);
}

void clip_fillet_tool_to_surface(TopoDS_Shape& tool,
        std::vector<TopoDS_Face>& treatment_faces,
        const TopoDS_Face& face,const gp_Pnt& inside,double tolerance,
        const gp_Pnt* retained_side_hint=nullptr) {
    if(face.IsNull())return;
    TopoDS_Face boundary=face;
    const BRepAdaptor_Surface support(face,true);
    // MakeHalfSpace interprets a trimmed cylindrical patch as an open shell;
    // it can therefore retain the extension outside the cylinder instead of
    // the material side.  Rebuild elementary terminals as their complete
    // analytic surface before selecting the side with the interior point.
    if(support.GetType()==GeomAbs_Cylinder) {
        BRepBuilderAPI_MakeFace complete(support.Cylinder());
        if(complete.IsDone())boundary=complete.Face();
    } else if(support.GetType()==GeomAbs_Cone) {
        BRepBuilderAPI_MakeFace complete(support.Cone());
        if(complete.IsDone())boundary=complete.Face();
    } else if(support.GetType()==GeomAbs_Sphere) {
        BRepBuilderAPI_MakeFace complete(support.Sphere());
        if(complete.IsDone())boundary=complete.Face();
    } else if(support.GetType()==GeomAbs_Torus) {
        BRepBuilderAPI_MakeFace complete(support.Torus());
        if(complete.IsDone())boundary=complete.Face();
    }
    // The authored route point lies on the source corner and therefore on the
    // cutter boundary.  It cannot reliably choose a curved half-space side.
    // Find a nearby point strictly inside the cutter, while remaining well
    // inside the unextended route interval, and retain that side.
    gp_Pnt retained_side=inside;
    BRepClass3d_SolidClassifier retained_classifier(
        tool,retained_side,tolerance);
    if(retained_classifier.State()!=TopAbs_IN) {
        Bnd_Box bounds;
        BRepBndLib::Add(tool,bounds);
        double x0{},y0{},z0{},x1{},y1{},z1{};
        bounds.Get(x0,y0,z0,x1,y1,z1);
        const double diagonal=gp_Pnt(x0,y0,z0).Distance(
            gp_Pnt(x1,y1,z1));
        const double base_step=std::max(1000.*tolerance,
            diagonal*1.0e-6);
        bool found{};
        for(const double multiplier:{1.,10.,100.}) {
            const double step=base_step*multiplier;
            for(int x=-1;x<=1&&!found;++x)
                for(int y=-1;y<=1&&!found;++y)
                    for(int z=-1;z<=1&&!found;++z) {
                        if(x==0&&y==0&&z==0)continue;
                        const gp_Pnt candidate=inside.Translated(
                            gp_Vec(x*step,y*step,z*step));
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
                "Planar edge Fillet could not locate the retained tool side");
    }
    // A separately swept route tail does not contain the preceding route
    // point, but that point still identifies which side connects back to the
    // authored route.  `inside` remains the robust tool-interior probe while
    // the optional hint selects the retained analytical half-space.
    BRepPrimAPI_MakeHalfSpace half_space(boundary,
        retained_side_hint?*retained_side_hint:retained_side);
    if(!half_space.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct an end half-space");
    clip_fillet_tool_with_halfspace(tool,treatment_faces,
        half_space.Solid(),tolerance);
}

bool same_curved_terminal_surface(const TopoDS_Face& first,
        const TopoDS_Face& second,double tolerance) {
    const BRepAdaptor_Surface a(first,true),b(second,true);
    if(a.GetType()!=b.GetType())return false;
    if(a.GetType()==GeomAbs_Cylinder) {
        const auto first_cylinder=a.Cylinder();
        const auto second_cylinder=b.Cylinder();
        return std::abs(first_cylinder.Radius()-second_cylinder.Radius())<=
                tolerance&&
            first_cylinder.Axis().Direction().IsParallel(
                second_cylinder.Axis().Direction(),1.0e-9)&&
            gp_Lin(first_cylinder.Axis()).Distance(
                second_cylinder.Axis().Location())<=tolerance;
    }
    if(a.GetType()==GeomAbs_Sphere) {
        const auto first_sphere=a.Sphere();
        const auto second_sphere=b.Sphere();
        return std::abs(first_sphere.Radius()-second_sphere.Radius())<=
                tolerance&&
            first_sphere.Location().Distance(second_sphere.Location())<=
                tolerance;
    }
    // Split faces normally retain the same underlying geometric surface.
    // This also covers an exact conical, toroidal or spline terminal without
    // conflating independently constructed but merely touching surfaces.
    return BRep_Tool::Surface(first)==BRep_Tool::Surface(second);
}

bool compound_curved_terminal(std::span<const TopoDS_Face> faces,
        double tolerance) {
    for(std::size_t first=0;first<faces.size();++first)
        for(std::size_t second=first+1;second<faces.size();++second)
            if(!same_curved_terminal_surface(
                    faces[first],faces[second],tolerance))
                return true;
    return false;
}

std::vector<TopoDS_Face> automatic_terminal_faces(
        const TopoDS_Solid& material,
        const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& tangent,const gp_Pnt& preceding_point,
        double tolerance) {
    struct Candidate {
        TopoDS_Face face;
        gp_Pln plane;
        int support_adjacency{};
    };
    std::vector<Candidate> candidates;
    for(TopExp_Explorer item(material,TopAbs_FACE);item.More();item.Next()) {
        const auto face=TopoDS::Face(item.Current());
        if(face.IsSame(first_support)||face.IsSame(second_support))continue;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane||
           support.Plane().Distance(endpoint)>tolerance)continue;
        const double incidence=std::abs(
            support.Plane().Axis().Direction().Dot(tangent));
        // A perpendicular cap already agrees with the exact endpoint. An
        // oblique plane and a plane tangent to a curved terminal both require
        // an explicit half-space clip. For the tangent case, the preceding
        // route point must leave the plane so it defines the retained side;
        // a straight route lying in the candidate plane is not terminated by
        // that plane.
        if(incidence>=1.-1.0e-8||
           (incidence<=1.0e-8&&
            support.Plane().Distance(preceding_point)<=tolerance))continue;
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            continue;
        const auto plane=support.Plane();
        const int adjacency=
            (terminal_face_meets_support_at_endpoint(face,first_support,
                endpoint,tolerance)?1:0)+
            (terminal_face_meets_support_at_endpoint(face,second_support,
                endpoint,tolerance)?1:0);
        const auto coplanar=std::ranges::find_if(candidates,
            [&](const auto& candidate) {
                return candidate.plane.Axis().Direction().IsParallel(
                        plane.Axis().Direction(),1.0e-9)&&
                    candidate.plane.Distance(plane.Location())<=tolerance;
            });
        if(coplanar==candidates.end())
            candidates.push_back({face,plane,adjacency});
        else if(adjacency>coplanar->support_adjacency)
            *coplanar={face,plane,adjacency};
    }
    if(candidates.empty())return {};
    const auto strongest=std::ranges::max_element(candidates,{},
        &Candidate::support_adjacency)->support_adjacency;
    std::vector<TopoDS_Face> result;
    // The terminal of an overgrown profile is the intersection of all local
    // material half-spaces, not one arbitrarily selected face.  Faces that
    // continue either support into the endpoint are topologically local and
    // may therefore participate together (for example at a stepped or
    // compound terminal).  With no support adjacency, retain the conservative
    // single-candidate rule rather than clipping against an unrelated plane
    // that merely passes through the same vertex.
    if(strongest>0)
        for(const auto& candidate:candidates)
            if(candidate.support_adjacency==strongest)
                result.push_back(candidate.face);
    else if(candidates.size()==1)
        result.push_back(candidates.front().face);
    return result;
}

bool has_perpendicular_terminal_face(const TopoDS_Solid& material,
        const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& tangent,double tolerance) {
    for(TopExp_Explorer item(material,TopAbs_FACE);item.More();item.Next()) {
        const auto face=TopoDS::Face(item.Current());
        if(face.IsSame(first_support)||face.IsSame(second_support))continue;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane||
           support.Plane().Distance(endpoint)>tolerance||
           std::abs(support.Plane().Axis().Direction().Dot(tangent))<
                1.-1.0e-8)
            continue;
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)
            return true;
    }
    return false;
}

double body_envelope_extension(const TopoDS_Shape& material,
        double cross_span,double tolerance) {
    Bnd_Box bounds;
    BRepBndLib::Add(material,bounds);
    if(bounds.IsVoid()||bounds.IsWhole())
        throw std::runtime_error(
            "Planar edge Fillet cannot bound its automatic end envelope");
    Standard_Real x_min{},y_min{},z_min{},x_max{},y_max{},z_max{};
    bounds.Get(x_min,y_min,z_min,x_max,y_max,z_max);
    const double diagonal=gp_Pnt(x_min,y_min,z_min).Distance(
        gp_Pnt(x_max,y_max,z_max));
    if(!std::isfinite(diagonal))
        throw std::runtime_error(
            "Planar edge Fillet automatic end envelope is invalid");
    return diagonal+cross_span+tolerance;
}

struct TangentFilletProfile {
    FilletProfile profile;
    gp_Pnt plane_contact;
    gp_Pnt cylinder_contact;
    gp_Pnt center;
    double span{};
};

struct RollingSupportSection {
    occ::handle<Geom_Curve> curve;
    double origin_parameter{},branch_parameter{};
    TopoDS_Edge edge;
};

struct RollingSupportProfile {
    FilletProfile profile;
    std::array<gp_Pnt,2> contacts;
    gp_Pnt center;
    double span{};
};

RollingSupportProfile translated_rolling_support_profile(
        const RollingSupportProfile& source,const gp_Vec& translation) {
    gp_Trsf transformation;
    transformation.SetTranslation(translation);
    BRepBuilderAPI_Transform transformed(source.profile.wire,transformation,
        true);
    if(!transformed.IsDone()||transformed.Shape().IsNull())
        throw std::runtime_error(
            "Planar edge Fillet could not position its rolling support profile");
    RollingSupportProfile result=source;
    result.profile.wire=TopoDS::Wire(transformed.Shape());
    const auto circular_edge=transformed.ModifiedShape(
        source.profile.circular_edge);
    if(circular_edge.IsNull()||circular_edge.ShapeType()!=TopAbs_EDGE)
        throw std::runtime_error(
            "Planar edge Fillet lost its rolling support arc while positioning it");
    result.profile.circular_edge=TopoDS::Edge(circular_edge);
    for(auto& contact:result.contacts)contact.Translate(translation);
    result.center.Translate(translation);
    return result;
}

bool support_is_prismatic_along(const TopoDS_Face& face,
        const gp_Dir& direction,double angular_tolerance) {
    const BRepAdaptor_Surface surface(face,true);
    switch(surface.GetType()) {
    case GeomAbs_Plane:
        return std::abs(surface.Plane().Axis().Direction().Dot(direction))<=
            angular_tolerance;
    case GeomAbs_Cylinder:
        return surface.Cylinder().Axis().Direction().IsParallel(direction,
            angular_tolerance);
    case GeomAbs_SurfaceOfExtrusion:
        return surface.Direction().IsParallel(direction,
            angular_tolerance);
    default:return false;
    }
}

std::optional<RollingSupportSection> rolling_support_section(
        const TopoDS_Face& face,const gp_Pln& section_plane,
        const gp_Pnt& origin,const gp_Dir& inward,
        const TopoDS_Vertex& origin_vertex,double span,double tolerance) {
    TopLoc_Location location;
    auto support_surface=BRep_Tool::Surface(face,location);
    if(!location.IsIdentity())
        support_surface=Handle(Geom_Surface)::DownCast(
            support_surface->Transformed(location.Transformation()));
    if(support_surface.IsNull())return std::nullopt;
    GeomAPI_IntSS intersection(support_surface,new Geom_Plane(section_plane),
        tolerance);
    if(!intersection.IsDone())return std::nullopt;
    occ::handle<Geom_Curve> curve;
    double origin_parameter{};
    double nearest=std::numeric_limits<double>::infinity();
    for(int index=1;index<=intersection.NbLines();++index) {
        const auto& candidate=intersection.Line(index);
        GeomAPI_ProjectPointOnCurve projection(origin,candidate);
        if(projection.NbPoints()==0||projection.LowerDistance()>nearest)
            continue;
        nearest=projection.LowerDistance();
        origin_parameter=projection.LowerDistanceParameter();
        curve=candidate;
    }
    if(curve.IsNull()||nearest>10.*tolerance)return std::nullopt;
    gp_Pnt point;gp_Vec derivative;
    curve->D1(origin_parameter,point,derivative);
    if(derivative.SquareMagnitude()<=1.0e-18)return std::nullopt;
    const double sense=derivative.Dot(gp_Vec(inward))>=0.?1.:-1.;
    GeomAdaptor_Curve adaptor(curve);
    double branch_length=span;
    if(curve->IsPeriodic())
        branch_length=std::min(branch_length,GCPnts_AbscissaPoint::Length(
            adaptor,origin_parameter,
            origin_parameter+sense*.49*curve->Period(),tolerance));
    GCPnts_AbscissaPoint branch(tolerance,adaptor,sense*branch_length,
        origin_parameter);
    if(!branch.IsDone())return std::nullopt;
    const double branch_parameter=branch.Parameter();
    const auto branch_point=curve->Value(branch_parameter);
    const auto branch_vertex=BRepBuilderAPI_MakeVertex(branch_point).Vertex();
    BRepBuilderAPI_MakeEdge edge;
    if(origin_parameter<branch_parameter)
        edge=BRepBuilderAPI_MakeEdge(curve,origin_vertex,branch_vertex,
            origin_parameter,branch_parameter);
    else
        edge=BRepBuilderAPI_MakeEdge(curve,branch_vertex,origin_vertex,
            branch_parameter,origin_parameter);
    if(!edge.IsDone())return std::nullopt;
    return RollingSupportSection{curve,origin_parameter,
        branch_parameter,edge.Edge()};
}

std::optional<RollingSupportProfile> rolling_support_profile(
        const gp_Pnt& origin,const gp_Dir& path_tangent,
        const gp_Dir& first_inward,const gp_Dir& second_inward,
        const TopoDS_Face& first_face,const TopoDS_Face& second_face,
        const TopoDS_Solid& material,double radius,double tolerance,
        bool adds_material) {
    const double angular_tolerance=std::max(1.0e-9,
        tolerance/std::max(1.,radius));
    if(!support_is_prismatic_along(first_face,path_tangent,
            angular_tolerance)||
       !support_is_prismatic_along(second_face,path_tangent,
            angular_tolerance))return std::nullopt;
    const bool first_planar=BRepAdaptor_Surface(first_face,true).GetType()==
        GeomAbs_Plane;
    const bool second_planar=BRepAdaptor_Surface(second_face,true).GetType()==
        GeomAbs_Plane;
    if(first_planar&&second_planar)return std::nullopt;
    const gp_Pln section_plane(origin,path_tangent);
    const auto origin_vertex=BRepBuilderAPI_MakeVertex(origin).Vertex();
    const double opening=first_inward.Angle(second_inward);
    const double local_span=4.*radius/std::max(.05,
        std::tan(opening/2.));
    const double section_span=std::min(
        body_envelope_extension(material,4.*radius,tolerance),
        std::max(8.*radius,local_span));
    const auto first=rolling_support_section(first_face,section_plane,
        origin,first_inward,origin_vertex,section_span,tolerance);
    const auto second=rolling_support_section(second_face,section_plane,
        origin,second_inward,origin_vertex,section_span,tolerance);
    if(!first||!second)return std::nullopt;

    auto first_edge=first->edge;
    auto second_edge=second->edge;
    const auto orient_to_origin=[&](TopoDS_Edge& edge,bool origin_first) {
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b,true);
        if((origin_first&&!a.IsSame(origin_vertex))||
           (!origin_first&&!b.IsSame(origin_vertex)))edge.Reverse();
    };
    orient_to_origin(first_edge,false);
    orient_to_origin(second_edge,true);
    ChFi2d_FilletAlgo fillet(first_edge,second_edge,section_plane);
    if(!fillet.Perform(radius))return std::nullopt;
    const int count=fillet.NbResults(origin);
    std::optional<RollingSupportProfile> best;
    double best_score=std::numeric_limits<double>::infinity();
    for(int solution=1;solution<=count;++solution) {
        TopoDS_Edge remaining_first,remaining_second;
        auto fillet_edge=fillet.Result(origin,remaining_first,
            remaining_second,solution);
        if(fillet_edge.IsNull()||
           BRepAdaptor_Curve(fillet_edge).GetType()!=GeomAbs_Circle)
            continue;
        std::array<TopoDS_Vertex,2> fillet_vertices;
        TopExp::Vertices(fillet_edge,fillet_vertices[0],
            fillet_vertices[1],true);
        if(fillet_vertices[0].IsNull()||fillet_vertices[1].IsNull())continue;
        const std::array<RollingSupportSection,2> sections{*first,*second};
        std::array<std::array<double,2>,2> parameters{};
        std::array<std::array<double,2>,2> distances{};
        for(std::size_t support=0;support<2;++support)
            for(std::size_t vertex=0;vertex<2;++vertex) {
                const double lower=std::min(sections[support].origin_parameter,
                    sections[support].branch_parameter);
                const double upper=std::max(sections[support].origin_parameter,
                    sections[support].branch_parameter);
                GeomAPI_ProjectPointOnCurve projection(
                    BRep_Tool::Pnt(fillet_vertices[vertex]),
                    sections[support].curve,lower,upper);
                if(projection.NbPoints()==0) {
                    distances[support][vertex]=
                        std::numeric_limits<double>::infinity();
                    continue;
                }
                distances[support][vertex]=projection.LowerDistance();
                parameters[support][vertex]=
                    projection.LowerDistanceParameter();
            }
        const double direct=distances[0][0]+distances[1][1];
        const double reverse=distances[0][1]+distances[1][0];
        const std::array<std::size_t,2> assignment=direct<=reverse
            ?std::array<std::size_t,2>{0,1}
            :std::array<std::size_t,2>{1,0};
        const double contact_error=std::min(direct,reverse);
        if(!std::isfinite(contact_error)||contact_error>20.*tolerance)
            continue;
        std::array<gp_Pnt,2> contacts;
        std::array<TopoDS_Edge,2> connectors;
        bool valid=true;
        for(std::size_t support=0;support<2;++support) {
            const auto vertex=assignment[support];
            contacts[support]=BRep_Tool::Pnt(fillet_vertices[vertex]);
            const double contact_parameter=parameters[support][vertex];
            BRepBuilderAPI_MakeEdge connector;
            if(sections[support].origin_parameter<contact_parameter)
                connector=BRepBuilderAPI_MakeEdge(sections[support].curve,
                    origin_vertex,fillet_vertices[vertex],
                    sections[support].origin_parameter,contact_parameter);
            else
                connector=BRepBuilderAPI_MakeEdge(sections[support].curve,
                    fillet_vertices[vertex],origin_vertex,contact_parameter,
                    sections[support].origin_parameter);
            if(!connector.IsDone()) {valid=false;break;}
            connectors[support]=connector.Edge();
            gp_Pnt support_point;gp_Vec support_derivative;
            sections[support].curve->D1(contact_parameter,support_point,
                support_derivative);
            const BRepAdaptor_Curve fillet_curve(fillet_edge);
            const bool at_first=contacts[support].Distance(
                fillet_curve.Value(fillet_curve.FirstParameter()))<=
                contacts[support].Distance(
                    fillet_curve.Value(fillet_curve.LastParameter()));
            gp_Pnt fillet_point;gp_Vec fillet_derivative;
            fillet_curve.D1(at_first?fillet_curve.FirstParameter():
                fillet_curve.LastParameter(),fillet_point,
                fillet_derivative);
            if(support_derivative.SquareMagnitude()<=1.0e-18||
               fillet_derivative.SquareMagnitude()<=1.0e-18||
               !gp_Dir(support_derivative).IsParallel(
                    gp_Dir(fillet_derivative),1.0e-7)) {
                valid=false;break;
            }
        }
        if(!valid)continue;
        BRepBuilderAPI_MakeWire wire;
        wire.Add(fillet_edge);wire.Add(connectors[0]);
        wire.Add(connectors[1]);
        if(!wire.IsDone())continue;
        BRepBuilderAPI_MakeFace face(wire.Wire());
        if(!face.IsDone()||!BRepCheck_Analyzer(face.Face()).IsValid())continue;
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face.Face(),properties);
        BRepClass3d_SolidClassifier classifier(material,
            properties.CentreOfMass(),tolerance);
        if((adds_material&&classifier.State()!=TopAbs_OUT)||
           (!adds_material&&classifier.State()!=TopAbs_IN))continue;
        const BRepAdaptor_Curve fillet_curve(fillet_edge);
        const auto center=fillet_curve.Circle().Location();
        const double score=origin.Distance(contacts[0])+origin.Distance(
            contacts[1]);
        if(score>=best_score)continue;
        RollingSupportProfile candidate;
        candidate.profile.circular_edge=fillet_edge;
        candidate.profile.wire=wire.Wire();
        candidate.contacts=contacts;
        candidate.center=center;
        candidate.span=std::max({score,origin.Distance(center)+radius,
            2.*radius});
        best=std::move(candidate);best_score=score;
    }
    return best;
}

TangentFilletProfile tangent_plane_cylinder_profile(
        const gp_Pnt& origin,const gp_Dir& path_tangent,
        const gp_Dir& first_inward,const gp_Dir& second_inward,
        const TopoDS_Face& first_face,const TopoDS_Face& second_face,
        double radius,double tolerance) {
    const BRepAdaptor_Surface first_surface(first_face,true);
    const BRepAdaptor_Surface second_surface(second_face,true);
    const bool first_is_plane=first_surface.GetType()==GeomAbs_Plane;
    const bool second_is_plane=second_surface.GetType()==GeomAbs_Plane;
    const bool first_is_cylinder=first_surface.GetType()==GeomAbs_Cylinder;
    const bool second_is_cylinder=second_surface.GetType()==GeomAbs_Cylinder;
    if(!((first_is_plane&&second_is_cylinder)||
         (second_is_plane&&first_is_cylinder)))
        throw std::invalid_argument(
            "Planar edge Fillet tangent transition requires one plane and one cylinder");
    const auto& plane_surface=first_is_plane?first_surface:second_surface;
    const auto& cylinder_surface=first_is_cylinder?first_surface:second_surface;
    const auto plane=plane_surface.Plane();
    const auto cylinder=cylinder_surface.Cylinder();
    const auto axis=cylinder.Axis();
    const double angular_tolerance=std::max(1.0e-9,
        tolerance/std::max(1.,cylinder.Radius()));
    if(!axis.Direction().IsParallel(path_tangent,angular_tolerance)||
       plane.Distance(origin)>tolerance)
        throw std::invalid_argument(
            "Planar edge Fillet tangent plane and cylinder do not follow the route");
    const gp_Vec axis_offset(axis.Location(),origin);
    const gp_Pnt cylinder_center=axis.Location().Translated(
        gp_Vec(axis.Direction())*axis_offset.Dot(gp_Vec(axis.Direction())));
    gp_Vec radial(cylinder_center,origin);
    if(std::abs(radial.Magnitude()-cylinder.Radius())>tolerance||
       radial.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet route is not on its cylindrical support");
    radial.Normalize();
    const auto plane_normal=plane.Axis().Direction();
    if(!plane_normal.IsParallel(gp_Dir(radial),angular_tolerance))
        throw std::invalid_argument(
            "Planar edge Fillet plane is not tangent to its cylindrical support");
    gp_Vec inward=gp_Vec(first_inward)+gp_Vec(second_inward);
    if(inward.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet tangent transition has no common material direction");
    inward.Normalize();
    if(std::abs(inward.Dot(radial))>angular_tolerance||
       std::abs(inward.Dot(gp_Vec(path_tangent)))>angular_tolerance)
        throw std::invalid_argument(
            "Planar edge Fillet tangent transition directions are inconsistent");

    // Cross-section of a rolling circle between a tangent plane and cylinder.
    // The new radius must pass beyond the degenerate plane/cylinder contact:
    // its axial-plane offset is exactly 2*sqrt(R*r).  Closing the profile with
    // the original cylinder arc (rather than a second straight helper) keeps
    // the Boolean tool equal to the real material that the Fillet adds.
    const double offset=2.*std::sqrt(cylinder.Radius()*radius);
    const gp_Pnt center=origin.Translated(
        inward*offset-radial*radius);
    const gp_Pnt plane_contact=origin.Translated(inward*offset);
    gp_Vec center_from_cylinder(cylinder_center,center);
    if(center_from_cylinder.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet tangent transition center is degenerate");
    center_from_cylinder.Normalize();
    const gp_Pnt cylinder_contact=cylinder_center.Translated(
        center_from_cylinder*cylinder.Radius());

    const auto arc_middle=[](const gp_Pnt& arc_center,
            const gp_Pnt& first,const gp_Pnt& last,double arc_radius) {
        gp_Vec first_radial(arc_center,first),last_radial(arc_center,last);
        first_radial.Normalize();last_radial.Normalize();
        gp_Vec middle=first_radial+last_radial;
        if(middle.SquareMagnitude()<=1.0e-18)
            throw std::invalid_argument(
                "Planar edge Fillet tangent transition arc is ambiguous");
        middle.Normalize();
        return arc_center.Translated(middle*arc_radius);
    };
    const auto fillet_arc=GC_MakeArcOfCircle(plane_contact,
        arc_middle(center,plane_contact,cylinder_contact,radius),
        cylinder_contact).Value();
    const auto support_arc=GC_MakeArcOfCircle(cylinder_contact,
        arc_middle(cylinder_center,cylinder_contact,origin,
            cylinder.Radius()),origin).Value();
    if(fillet_arc.IsNull()||support_arc.IsNull())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its tangent transition arcs");
    TangentFilletProfile result;
    result.profile.circular_edge=BRepBuilderAPI_MakeEdge(fillet_arc).Edge();
    BRepBuilderAPI_MakeWire wire;
    wire.Add(result.profile.circular_edge);
    wire.Add(BRepBuilderAPI_MakeEdge(support_arc).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(origin,plane_contact).Edge());
    if(!wire.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not close its tangent transition profile");
    result.profile.wire=wire.Wire();
    result.plane_contact=plane_contact;
    result.cylinder_contact=cylinder_contact;
    result.center=center;
    result.span=offset+2.*radius;
    return result;
}

Cutter make_tangent_plane_cylinder_cutter(
        const TopoDS_Face& first_face,const TopoDS_Face& second_face,
        const EdgePath& path,const gp_Dir& first_inward,
        const gp_Dir& second_inward,double radius,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces,bool profile_only,
        OcctPlanarEdgeFilletEndMode end_mode) {
    if(path.kind!=EdgeFilletPathKind::Linear)
        throw std::invalid_argument(
            "Planar edge Fillet tangent plane-cylinder transition requires a straight route");
    const auto midpoint=tangent_plane_cylinder_profile(path.middle,
        path.first_tangent,first_inward,second_inward,first_face,second_face,
        radius,tolerance);
    const auto analytic=[&](const TangentFilletProfile& profile,
            double extension) {
        const bool first_plane=BRepAdaptor_Surface(first_face,true).GetType()==
            GeomAbs_Plane;
        return PlanarFilletAnalyticRemovalDomain{
            point(path.first),point(path.last),
            vector(path.first_tangent.XYZ()),
            vector(first_inward.XYZ()),vector(second_inward.XYZ()),
            point(first_plane?profile.plane_contact:profile.cylinder_contact),
            point(first_plane?profile.cylinder_contact:profile.plane_contact),
            point(profile.center),radius,extension,path.kind,{},{},0.,0.};
    };
    if(profile_only)
        return {{},{},analytic(midpoint,0.),true,midpoint.profile.wire,
            midpoint.profile.circular_edge,{}};

    const gp_Pnt interior((path.middle.X()+midpoint.plane_contact.X()+
            midpoint.cylinder_contact.X())/3.,
        (path.middle.Y()+midpoint.plane_contact.Y()+
            midpoint.cylinder_contact.Y())/3.,
        (path.middle.Z()+midpoint.plane_contact.Z()+
            midpoint.cylinder_contact.Z())/3.);
    BRepClass3d_SolidClassifier classifier(material,interior,tolerance);
    if(classifier.State()!=TopAbs_OUT)
        throw std::invalid_argument(
            "Planar edge Fillet tangent transition does not bound an additive material pocket");

    std::array<std::vector<TopoDS_Face>,2> terminal_faces;
    if(!end_faces.first.IsNull())terminal_faces[0].push_back(end_faces.first);
    if(!end_faces.last.IsNull())terminal_faces[1].push_back(end_faces.last);
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(terminal_faces[0].empty())
            terminal_faces[0]=automatic_terminal_faces(material,
                first_face,second_face,path.first,path.first_tangent,
                path.middle,tolerance);
        if(terminal_faces[1].empty())
            terminal_faces[1]=automatic_terminal_faces(material,
                first_face,second_face,path.last,path.last_tangent,
                path.middle,tolerance);
    }
    const auto extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,const gp_Dir& tangent) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane||
           support.Plane().Distance(endpoint)>tolerance)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        BRepClass_FaceClassifier face_classifier(face,endpoint,tolerance);
        if(face_classifier.State()!=TopAbs_IN&&
           face_classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        const double incidence=std::abs(
            support.Plane().Axis().Direction().Dot(tangent));
        return incidence<=1.0e-8?0.:
            midpoint.span/incidence+tolerance;
    };
    const auto end_extension=[&](const auto& faces,const gp_Pnt& endpoint,
            const gp_Dir& tangent) {
        double result{};
        for(const auto& face:faces)
            result=std::max(result,extension(face,endpoint,tangent));
        return result;
    };
    const double first_extension=end_extension(terminal_faces[0],
        path.first,path.first_tangent);
    const double last_extension=end_extension(terminal_faces[1],
        path.last,path.last_tangent);
    const gp_Pnt source_origin=path.first.Translated(
        -gp_Vec(path.first_tangent)*first_extension);
    const auto source=tangent_plane_cylinder_profile(source_origin,
        path.first_tangent,first_inward,second_inward,first_face,second_face,
        radius,tolerance);
    BRepBuilderAPI_MakeFace section(source.profile.wire);
    if(!section.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its tangent transition section");
    BRepPrimAPI_MakePrism prism(section.Face(),gp_Vec(path.first_tangent)*
        (path.length+first_extension+last_extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not construct a valid tangent transition tool");
    std::vector<TopoDS_Face> circular_faces;
    for(const auto& generated:prism.Generated(source.profile.circular_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            circular_faces.push_back(TopoDS::Face(generated));
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet tangent transition omitted its circular face");
    TopoDS_Shape tool=prism.Shape();
    for(const auto& face:terminal_faces[0])
        clip_fillet_tool(tool,circular_faces,face,path.middle,tolerance);
    for(const auto& face:terminal_faces[1])
        clip_fillet_tool(tool,circular_faces,face,path.middle,tolerance);
    return {tool,std::move(circular_faces),
        analytic(source,std::max(first_extension,last_extension)),true,
        midpoint.profile.wire,midpoint.profile.circular_edge,{}};
}

std::optional<Cutter> make_rolling_support_cutter(
        const TopoDS_Face& first_face,const TopoDS_Face& second_face,
        const EdgePath& path,const gp_Dir& first_inward,
        const gp_Dir& second_inward,double radius,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces,bool profile_only,
        OcctPlanarEdgeFilletEndMode end_mode,bool adds_material) {
    if(path.kind!=EdgeFilletPathKind::Linear)return std::nullopt;
    const auto midpoint=rolling_support_profile(path.middle,
        path.first_tangent,first_inward,second_inward,first_face,
        second_face,material,radius,tolerance,adds_material);
    if(!midpoint)return std::nullopt;
    const auto analytic=[&](const RollingSupportProfile& profile,
            double extension) {
        return PlanarFilletAnalyticRemovalDomain{
            point(path.first),point(path.last),
            vector(path.first_tangent.XYZ()),
            vector(first_inward.XYZ()),vector(second_inward.XYZ()),
            point(profile.contacts[0]),point(profile.contacts[1]),
            point(profile.center),radius,extension,path.kind,{},{},0.,0.};
    };
    if(profile_only)
        return Cutter{{},{},analytic(*midpoint,0.),adds_material,
            midpoint->profile.wire,midpoint->profile.circular_edge,{}};

    std::array<std::vector<TopoDS_Face>,2> terminal_faces;
    if(!end_faces.first.IsNull())terminal_faces[0].push_back(end_faces.first);
    if(!end_faces.last.IsNull())terminal_faces[1].push_back(end_faces.last);
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(terminal_faces[0].empty())
            terminal_faces[0]=automatic_terminal_faces(material,
                first_face,second_face,path.first,path.first_tangent,
                path.middle,tolerance);
        if(terminal_faces[1].empty())
            terminal_faces[1]=automatic_terminal_faces(material,
                first_face,second_face,path.last,path.last_tangent,
                path.middle,tolerance);
    }
    const auto extension=[&](const auto& faces,const gp_Pnt& endpoint,
            const gp_Dir& outward) {
        double result{};
        for(const auto& face:faces) {
            const BRepAdaptor_Surface support(face,true);
            if(support.GetType()!=GeomAbs_Plane)
                throw std::invalid_argument(
                    "Planar edge Fillet end face must be planar");
            const auto normal=support.Plane().Axis().Direction();
            const double signed_incidence=normal.Dot(outward);
            const double incidence=std::abs(signed_incidence);
            double distance{};
            if(support.Plane().Distance(endpoint)>tolerance) {
                if(incidence<=1.0e-8)
                    throw std::invalid_argument(
                        "Planar edge Fillet remote end face does not intersect the route");
                distance=gp_Vec(endpoint,support.Plane().Location()).Dot(
                    gp_Vec(normal))/signed_incidence;
                if(distance<=tolerance)
                    throw std::invalid_argument(
                        "Planar edge Fillet remote end face is behind the route endpoint");
            }
            result=std::max(result,distance+(incidence<=1.0e-8
                ?midpoint->span:midpoint->span/incidence+tolerance));
        }
        return result;
    };
    const gp_Dir first_outward(-gp_Vec(path.first_tangent));
    const gp_Dir last_outward(path.last_tangent);
    const double first_extension=extension(terminal_faces[0],path.first,
        first_outward);
    const double last_extension=extension(terminal_faces[1],path.last,
        last_outward);
    const gp_Pnt source_origin=path.first.Translated(
        gp_Vec(first_outward)*first_extension);
    const auto source=translated_rolling_support_profile(*midpoint,
        gp_Vec(path.middle,source_origin));
    BRepBuilderAPI_MakeFace section(source.profile.wire);
    if(!section.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its rolling support section");
    BRepPrimAPI_MakePrism prism(section.Face(),gp_Vec(path.first_tangent)*
        (path.length+first_extension+last_extension),false,true);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().IsNull()||
       !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not construct a valid rolling support tool");
    std::vector<TopoDS_Face> circular_faces;
    for(const auto& generated:prism.Generated(source.profile.circular_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            circular_faces.push_back(TopoDS::Face(generated));
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet rolling support omitted its circular face");
    TopoDS_Shape tool=prism.Shape();
    for(const auto& face:terminal_faces[0])
        clip_fillet_tool(tool,circular_faces,face,path.middle,tolerance);
    for(const auto& face:terminal_faces[1])
        clip_fillet_tool(tool,circular_faces,face,path.middle,tolerance);
    return Cutter{tool,std::move(circular_faces),
        analytic(source,std::max(first_extension,last_extension)),
        adds_material,midpoint->profile.wire,
        midpoint->profile.circular_edge,{}};
}

Cutter make_cutter(const TopoDS_Edge& edge,
        const TopoDS_Face& first_face,
        const TopoDS_Face& second_face,double radius,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces,
        bool profile_only=false,
        OcctPlanarEdgeFilletEndMode end_mode=
            OcctPlanarEdgeFilletEndMode::Perpendicular,
        CutterEndExtensions node_extensions={}) {
    const BRepAdaptor_Curve curve(edge);
    const auto path=edge_path(edge,tolerance);
    const auto direction_at=[&](double parameter) {
        gp_Pnt point;gp_Vec derivative;
        curve.D1(parameter,point,derivative);
        if(derivative.SquareMagnitude()<=1.0e-18)
            throw std::invalid_argument(
                "Planar edge Fillet support direction is degenerate");
        return gp_Dir(derivative);
    };
    // Build every treatment profile at the path midpoint.  Besides making
    // circular paths numerically symmetric, this keeps both ends of a linear
    // path independent of the edge's authored orientation.
    constexpr double anchor_fraction=.5;
    const double anchor_parameter=path.middle_parameter;
    const gp_Pnt anchor_point=curve.Value(anchor_parameter);
    const auto anchor_tangent=direction_at(anchor_parameter);
    const gp_Dir first_inward=inward_direction(
        edge,first_face,curve,anchor_parameter,
        anchor_tangent,tolerance);
    const gp_Dir second_inward=inward_direction(
        edge,second_face,curve,anchor_parameter,
        anchor_tangent,tolerance);
    const double interior_angle=first_inward.Angle(second_inward);
    if(interior_angle<=1.0e-6)
        return make_tangent_plane_cylinder_cutter(first_face,second_face,
            path,first_inward,second_inward,radius,tolerance,
            material,end_faces,profile_only,end_mode);
    if(!(interior_angle>1.0e-6&&
         interior_angle<std::numbers::pi-1.0e-6))
        throw std::invalid_argument(
            "Planar edge Fillet has no finite material wedge");
    gp_Vec bisector=gp_Vec(first_inward)+gp_Vec(second_inward);
    if(bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet material bisector is degenerate");
    bisector.Normalize();
    const double angular_tolerance=std::max(1.0e-9,
        tolerance/std::max({1.,path.length,path.path_radius}));
    gp_Dir middle_first_inward=first_inward;
    gp_Dir middle_second_inward=second_inward;
    for(const double fraction:{0.,.5,1.}) {
        if(fraction==anchor_fraction)continue;
        const double parameter=path.first_parameter+fraction*
            (path.last_parameter-path.first_parameter);
        const auto tangent=direction_at(parameter);
        const auto actual_first=inward_direction(edge,first_face,curve,
            parameter,tangent,tolerance);
        const auto actual_second=inward_direction(edge,second_face,curve,
            parameter,tangent,tolerance);
        const gp_Dir expected_first(transported(path,
            gp_Vec(first_inward),(fraction-anchor_fraction)*path.length));
        const gp_Dir expected_second(transported(path,
            gp_Vec(second_inward),(fraction-anchor_fraction)*path.length));
        if(actual_first.Angle(expected_first)>angular_tolerance||
           actual_second.Angle(expected_second)>angular_tolerance||
           std::abs(actual_first.Angle(actual_second)-interior_angle)>
                angular_tolerance)
            throw std::invalid_argument(
                "Planar edge Fillet support faces do not follow the selected path");
        if(fraction==.5) {
            middle_first_inward=actual_first;
            middle_second_inward=actual_second;
        }
    }
    gp_Vec middle_bisector=gp_Vec(middle_first_inward)+
        gp_Vec(middle_second_inward);
    if(middle_bisector.SquareMagnitude()<=1.0e-18)
        throw std::invalid_argument(
            "Planar edge Fillet material bisector is degenerate");
    middle_bisector.Normalize();
    const double probe=std::min(radius*.1,
        std::max(10.*tolerance,path.length*1.0e-5));
    BRepClass3d_SolidClassifier wedge_classifier(material,
        path.middle.Translated(middle_bisector*probe),tolerance);
    BRepClass3d_SolidClassifier opposite_classifier(material,
        path.middle.Translated(-middle_bisector*probe),tolerance);
    const bool removes_material=wedge_classifier.State()==TopAbs_IN&&
        opposite_classifier.State()==TopAbs_OUT;
    const bool adds_material=wedge_classifier.State()==TopAbs_OUT&&
        opposite_classifier.State()==TopAbs_IN;
    if(!removes_material&&!adds_material)
        throw std::invalid_argument(
            "Planar edge Fillet could not classify the material wedge");

    // A straight rolling path bounded by an extruded circle, ellipse or spline
    // needs the real support sections.  Replacing a curved support by its
    // tangent at the selected edge creates the right nominal radius but leaves
    // a visible C0 seam.  ChFi2d supplies the local arc/curve/curve profile;
    // the fast plane/plane implementation below remains unchanged.
    const bool rolling_support_required=
        path.kind==EdgeFilletPathKind::Linear&&
        support_is_prismatic_along(first_face,path.first_tangent,
            angular_tolerance)&&
        support_is_prismatic_along(second_face,path.first_tangent,
            angular_tolerance)&&
        (BRepAdaptor_Surface(first_face,true).GetType()!=GeomAbs_Plane||
         BRepAdaptor_Surface(second_face,true).GetType()!=GeomAbs_Plane);
    if(rolling_support_required) {
        if(auto rolling=make_rolling_support_cutter(first_face,second_face,path,
                first_inward,second_inward,radius,tolerance,material,end_faces,
                profile_only,end_mode,adds_material))return std::move(*rolling);
        throw std::invalid_argument(
            "Planar edge Fillet could not construct an exact tangent rolling profile");
    }

    const double tangent_distance=radius/
        std::tan(interior_angle/2.);
    const double center_distance=radius/
        std::sin(interior_angle/2.);
    if(!std::isfinite(tangent_distance)||
       !std::isfinite(center_distance))
        throw std::invalid_argument(
            "Planar edge Fillet radius is invalid");
    std::array<std::vector<TopoDS_Face>,2> terminal_faces;
    std::array<std::vector<TopoDS_Face>,2> curved_terminal_faces;
    const bool explicit_first=!end_faces.first.IsNull();
    const bool explicit_last=!end_faces.last.IsNull();
    if(explicit_first)terminal_faces[0].push_back(end_faces.first);
    if(explicit_last)terminal_faces[1].push_back(end_faces.last);
    const bool automatic_request=!profile_only&&removes_material&&
        path.kind==EdgeFilletPathKind::Linear&&end_mode==
            OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope;
    if(!profile_only&&end_mode==
            OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(terminal_faces[0].empty())
            terminal_faces[0]=automatic_terminal_faces(
                material,first_face,second_face,path.first,
                path.first_tangent,path.middle,tolerance);
        if(terminal_faces[1].empty())
            terminal_faces[1]=automatic_terminal_faces(
                material,first_face,second_face,path.last,
                path.last_tangent,path.middle,tolerance);
        curved_terminal_faces[0]=local_curved_terminal_faces(material,
            first_face,second_face,path.first,tolerance);
        curved_terminal_faces[1]=local_curved_terminal_faces(material,
            first_face,second_face,path.last,tolerance);
    }
    // Two independent routes meeting at one selected node need their rolling
    // cylinders to reach the common axis intersection. Suppress the ordinary
    // endpoint cap only at that node; the caller trims both extended sectors
    // by their shared miter plane instead of retaining circular end wedges.
    if(node_extensions.first>tolerance) {
        terminal_faces[0].clear();
        curved_terminal_faces[0].clear();
    }
    if(node_extensions.last>tolerance) {
        terminal_faces[1].clear();
        curved_terminal_faces[1].clear();
    }
    const gp_Dir first_outward(-gp_Vec(path.first_tangent));
    const gp_Dir last_outward(path.last_tangent);
    const bool local_curved_first=!curved_terminal_faces[0].empty();
    const bool local_curved_last=!curved_terminal_faces[1].empty();
    const bool compound_curved_first=compound_curved_terminal(
        curved_terminal_faces[0],tolerance);
    const bool compound_curved_last=compound_curved_terminal(
        curved_terminal_faces[1],tolerance);
    // An additive corner ends at the first preceding radius.  A local planar
    // face may share that endpoint, but it must not overgrow the additive
    // tool past the arc.  Explicit Up To input remains authoritative.
    if(adds_material&&local_curved_first&&!explicit_first)
        terminal_faces[0].clear();
    if(adds_material&&local_curved_last&&!explicit_last)
        terminal_faces[1].clear();
    const gp_Dir first_terminal_first_inward(transported(path,
        gp_Vec(first_inward),-anchor_fraction*path.length));
    const gp_Dir first_terminal_second_inward(transported(path,
        gp_Vec(second_inward),-anchor_fraction*path.length));
    const gp_Dir last_terminal_first_inward(transported(path,
        gp_Vec(first_inward),(1.-anchor_fraction)*path.length));
    const gp_Dir last_terminal_second_inward(transported(path,
        gp_Vec(second_inward),(1.-anchor_fraction)*path.length));
    const auto crossing_terminal_faces=[&](
            std::span<const TopoDS_Face> faces,const gp_Pnt& endpoint,
            const gp_Dir& outward) {
        std::vector<TopoDS_Face> result;
        for(const auto& face:faces)
            if(route_crosses_curved_terminal(
                    std::span(&face,1),endpoint,outward,tolerance))
                result.push_back(face);
        return result;
    };
    auto route_crossing_first=!explicit_first
        ?crossing_terminal_faces(curved_terminal_faces[0],path.first,
            first_outward)
        :std::vector<TopoDS_Face>{};
    auto route_crossing_last=!explicit_last
        ?crossing_terminal_faces(curved_terminal_faces[1],path.last,
            last_outward)
        :std::vector<TopoDS_Face>{};
    // A route which crosses a curved Body envelope must be overgrown and
    // clipped by that envelope.  An automatically inferred planar cap at the
    // same vertex would otherwise truncate the tool before the curved clip
    // can establish its real intersection.
    if(!route_crossing_first.empty())terminal_faces[0].clear();
    if(!route_crossing_last.empty())terminal_faces[1].clear();
    // The material-side probe distinguishes an earlier radius which bends into
    // this removal (exact local stop) from one which bends away (continue only
    // to its first adjoining planar cap).  The same decision applies when a
    // Boolean represents that local radius with different curved faces.
    const bool continue_first=local_curved_first&&!adds_material&&
        !explicit_first&&route_crossing_first.empty()&&
        removal_profile_continues_past_curved_terminal(
            material,path.first,first_outward,
            first_terminal_first_inward,first_terminal_second_inward,
            radius,tolerance);
    const bool continue_last=local_curved_last&&!adds_material&&
        !explicit_last&&route_crossing_last.empty()&&
        removal_profile_continues_past_curved_terminal(
            material,path.last,last_outward,
            last_terminal_first_inward,last_terminal_second_inward,
            radius,tolerance);
    // A treatment which continues beyond an earlier radius uses the first
    // real planar cap adjacent to that radius.  This preserves the visible
    // result of a historical full-edge selection while consuming only the
    // one current Body edge.  When the curved terminal bends into a removal
    // wedge, `continue_*` is false and the arc remains the exact endpoint.
    if(continue_first)
        terminal_faces[0]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,path.first,first_outward,
            tolerance);
    if(continue_last)
        terminal_faces[1]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,path.last,last_outward,
            tolerance);
    const bool envelope_first=automatic_request&&
        terminal_faces[0].empty()&&!local_curved_first&&
        !has_perpendicular_terminal_face(material,first_face,second_face,
            path.first,path.first_tangent,tolerance);
    const bool envelope_last=automatic_request&&
        terminal_faces[1].empty()&&!local_curved_last&&
        !has_perpendicular_terminal_face(material,first_face,second_face,
            path.last,path.last_tangent,tolerance);
    const auto axial_extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,const gp_Dir& outward,
            double cross_span) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)
            throw std::invalid_argument(
                "Planar edge Fillet end face must be planar");
        const auto normal=support.Plane().Axis().Direction();
        const double signed_incidence=normal.Dot(outward);
        const double incidence=std::abs(signed_incidence);
        if(support.Plane().Distance(endpoint)>tolerance) {
            if(incidence<=1.0e-8)
                throw std::invalid_argument(
                    "Planar edge Fillet remote end face does not intersect the route");
            const double distance=gp_Vec(endpoint,
                support.Plane().Location()).Dot(gp_Vec(normal)) /
                signed_incidence;
            if(distance<=tolerance)
                throw std::invalid_argument(
                    "Planar edge Fillet remote end face is behind the route endpoint");
            return distance+cross_span/incidence+tolerance;
        }
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        // A terminal plane tangent to a curved route cannot trim a tool that
        // stops exactly at the route endpoint: the profile can lie entirely
        // on the retained side and the Boolean degenerates to a tangential
        // common.  Deliberately overgrow it by one complete profile span and
        // let the terminal half-space establish the exact end below.
        if(incidence<=1.0e-8)return cross_span;
        return cross_span/incidence+tolerance;
    };
    const gp_Pnt anchor_first_tangent=anchor_point.Translated(
        gp_Vec(first_inward)*tangent_distance);
    const gp_Pnt anchor_second_tangent=anchor_point.Translated(
        gp_Vec(second_inward)*tangent_distance);
    const gp_Pnt anchor_center=anchor_point.Translated(
        bisector*center_distance);
    const auto midpoint_profile=fillet_profile(anchor_point,first_inward,
        second_inward,bisector,tangent_distance,center_distance,radius);
    const double anchor_offset=anchor_fraction*path.length;
    const gp_Pnt source_first_tangent=transported(path,
        anchor_first_tangent,-anchor_offset);
    const gp_Pnt source_second_tangent=transported(path,
        anchor_second_tangent,-anchor_offset);
    const gp_Pnt source_center=transported(path,anchor_center,
        -anchor_offset);
    const gp_Dir source_first_inward(transported(path,
        gp_Vec(first_inward),-anchor_offset));
    const gp_Dir source_second_inward(transported(path,
        gp_Vec(second_inward),-anchor_offset));
    const auto analytic_domain=[&](double extension) {
        return PlanarFilletAnalyticRemovalDomain{
            point(path.first),point(path.last),
            vector(path.first_tangent.XYZ()),
            vector(source_first_inward.XYZ()),
            vector(source_second_inward.XYZ()),
            point(source_first_tangent),point(source_second_tangent),
            point(source_center),radius,extension,path.kind,
            point(path.revolution_axis.Location()),
            vector(path.revolution_axis.Direction().XYZ()),
            path.path_radius,path.sweep_angle};
    };
    if(profile_only)
        return {{},{},analytic_domain(0.),adds_material,
            midpoint_profile.wire,midpoint_profile.circular_edge,{}};
    // The exact treatment profile is the circular arc closed by the two
    // support-face segments meeting at the source edge. Do not extend this
    // closure outside the original corner: an additive Fillet could retain
    // such an extension as visible material after the Boolean fuse.
    const double cross_span=2.*(tangent_distance+radius);
    const double envelope_extension=envelope_first||envelope_last
        ?body_envelope_extension(material,cross_span,tolerance):0.;
    const auto end_extension=[&](const auto& faces,const gp_Pnt& endpoint,
            const gp_Dir& tangent,bool envelope) {
        if(faces.empty())return envelope?envelope_extension:0.;
        double result{};
        for(const auto& face:faces)
            result=std::max(result,axial_extension(
                face,endpoint,tangent,cross_span));
        return result;
    };
    double first_extension=end_extension(terminal_faces[0],path.first,
        first_outward,envelope_first);
    double last_extension=end_extension(terminal_faces[1],path.last,
        last_outward,envelope_last);
    first_extension=std::max(first_extension,node_extensions.first);
    last_extension=std::max(last_extension,node_extensions.last);
    double first_preview_extension=first_extension;
    double last_preview_extension=last_extension;
    const bool trim_to_curved_first=!continue_first&&!explicit_first&&
        !curved_terminal_faces[0].empty()&&
        (!route_crossing_first.empty()||!compound_curved_first);
    const bool trim_to_curved_last=!continue_last&&!explicit_last&&
        !curved_terminal_faces[1].empty()&&
        (!route_crossing_last.empty()||!compound_curved_last);
    std::array<std::vector<TopoDS_Face>,2> curved_terminal_caps;
    if(trim_to_curved_first) {
        curved_terminal_caps[0]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,path.first,first_outward,
            tolerance);
        // The actual construction must reach the remote cap so that the
        // analytical surface and the cap can trim the same overgrown tool.
        // Keep the visible analytic preview local to the endpoint profile.
        first_preview_extension=std::max(first_preview_extension,cross_span);
        first_extension=std::max(first_extension,cross_span);
        first_extension=std::max(first_extension,end_extension(
            curved_terminal_caps[0],path.first,first_outward,false));
    }
    if(trim_to_curved_last) {
        curved_terminal_caps[1]=remote_terminal_faces_through_local_curve(
            material,first_face,second_face,path.last,last_outward,
            tolerance);
        last_preview_extension=std::max(last_preview_extension,cross_span);
        last_extension=std::max(last_extension,cross_span);
        last_extension=std::max(last_extension,end_extension(
            curved_terminal_caps[1],path.last,last_outward,false));
    }
    // Explicit ZIMA keeps a null end perpendicular. Automatic first honors a
    // perpendicular cap or a uniquely inferred oblique plane. Only a complex
    // unresolved CUT terminal uses the Body envelope; the actual material
    // clips that subtraction without adding anything outside the model.
    if(path.kind==EdgeFilletPathKind::Circular&&
       first_extension+last_extension>tolerance&&
       path.sweep_angle+(first_extension+last_extension)/
            path.path_radius>2.*std::numbers::pi-1.0e-10)
        throw std::invalid_argument(
            "Planar edge Fillet circular end extension overlaps itself");
    const double preview_extension=std::max(
        first_preview_extension,last_preview_extension);
    const gp_Pnt origin=transported(path,anchor_point,
        -anchor_offset-first_extension);
    const gp_Dir extended_first_inward(transported(path,
        gp_Vec(first_inward),-anchor_offset-first_extension));
    const gp_Dir extended_second_inward(transported(path,
        gp_Vec(second_inward),-anchor_offset-first_extension));
    gp_Vec extended_bisector=transported(path,bisector,
        -anchor_offset-first_extension);
    extended_bisector.Normalize();
    const auto source_profile=fillet_profile(origin,extended_first_inward,
        extended_second_inward,extended_bisector,tangent_distance,
        center_distance,radius);
    BRepBuilderAPI_MakeFace section(source_profile.wire);
    if(!section.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct its cutter section");
    TopoDS_Shape tool;
    std::vector<TopoDS_Face> circular_faces;
    if(path.kind==EdgeFilletPathKind::Linear) {
        BRepPrimAPI_MakePrism prism(section.Face(),
            gp_Vec(path.first_tangent)*
                (path.length+first_extension+last_extension),false,true);
        prism.Build();
        if(!prism.IsDone()||prism.Shape().IsNull()||
           !BRepCheck_Analyzer(prism.Shape()).IsValid())
            throw std::runtime_error(
                "Planar edge Fillet could not construct a valid cutter");
        tool=prism.Shape();
        for(const auto& generated:prism.Generated(
                source_profile.circular_edge))
            if(generated.ShapeType()==TopAbs_FACE)
                circular_faces.push_back(TopoDS::Face(generated));
    } else {
        BRepPrimAPI_MakeRevol revolution(section.Face(),
            path.revolution_axis,
            path.sweep_angle+(first_extension+last_extension)/
                path.path_radius,true);
        revolution.Build();
        if(!revolution.IsDone()||revolution.Shape().IsNull()||
           !BRepCheck_Analyzer(revolution.Shape()).IsValid())
            throw std::runtime_error(
                "Planar edge Fillet could not construct a valid cutter");
        tool=revolution.Shape();
        for(const auto& generated:revolution.Generated(
                source_profile.circular_edge))
            if(generated.ShapeType()==TopAbs_FACE)
                circular_faces.push_back(TopoDS::Face(generated));
    }
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet cutter omitted its circular face");
    const gp_Pnt interior=path.middle;
    for(const auto& face:terminal_faces[0])
        clip_fillet_tool(tool,circular_faces,face,interior,tolerance);
    for(const auto& face:terminal_faces[1])
        clip_fillet_tool(tool,circular_faces,face,interior,tolerance);
    // Extend the profile through a curved terminal and trim the construction
    // tool to the terminal's exact analytical surface before the Body Boolean.
    // Letting a subtractive tool cross the Body envelope makes the later CUT
    // split that envelope and can retain a small same-domain tongue at the
    // route endpoint.
    if(first_extension>tolerance&&trim_to_curved_first)
        for(const auto& face:route_crossing_first.empty()
                ?curved_terminal_faces[0]:route_crossing_first)
            clip_fillet_tool_to_surface(tool,circular_faces,face,
                interior,tolerance);
    if(last_extension>tolerance&&trim_to_curved_last)
        for(const auto& face:route_crossing_last.empty()
                ?curved_terminal_faces[1]:route_crossing_last)
            clip_fillet_tool_to_surface(tool,circular_faces,face,
                interior,tolerance);
    // A curved terminal is a trimmed Body patch, not its unbounded analytic
    // cylinder/cone alone.  Its remote planar caps complete the trimming
    // volume, especially for additive tools where any unbounded remainder is
    // visible as a tooth after the Fuse.
    for(const auto& face:curved_terminal_caps[0])
        clip_fillet_tool(tool,circular_faces,face,interior,tolerance);
    for(const auto& face:curved_terminal_caps[1])
        clip_fillet_tool(tool,circular_faces,face,interior,tolerance);
    return {tool,std::move(circular_faces),analytic_domain(preview_extension),
        adds_material,midpoint_profile.wire,
        midpoint_profile.circular_edge,{}};
}

bool route_stays_in_retained_halfspace(
        std::span<const TopoDS_Edge> route,const TopoDS_Face& terminal,
        const gp_Pnt& retained_point,double tolerance) {
    if(terminal.IsNull())return false;
    const BRepAdaptor_Surface support(terminal,true);
    if(support.GetType()!=GeomAbs_Plane)return false;
    const auto plane=support.Plane();
    const gp_Vec normal(plane.Axis().Direction());
    const auto signed_distance=[&](const gp_Pnt& point) {
        return gp_Vec(plane.Location(),point).Dot(normal);
    };
    const double retained_distance=signed_distance(retained_point);
    const double boundary_tolerance=10.*tolerance;
    if(std::abs(retained_distance)<=boundary_tolerance)return false;
    const double retained_sign=retained_distance>0.?1.:-1.;
    for(const auto& edge:route) {
        const BRepAdaptor_Curve curve(edge);
        // The exact extrema proof below is intentionally limited to the two
        // analytic paths handled by EdgePath.  A spline member can still be
        // swept as part of a connected route, but endpoint sampling would not
        // prove that its interior stays in the retained half-space.  In that
        // case leave the base sweep untouched and let the Body Boolean and
        // separately clipped terminal extension establish the result.
        if(curve.GetType()!=GeomAbs_Line&&
           curve.GetType()!=GeomAbs_Circle)return false;
        const auto path=edge_path(edge,tolerance);
        double minimum=std::min(
            retained_sign*signed_distance(path.first),
            retained_sign*signed_distance(path.last));
        if(path.kind==EdgeFilletPathKind::Circular) {
            const auto circle=curve.Circle();
            const auto axes=circle.Position();
            const double constant=retained_sign*
                signed_distance(circle.Location());
            const double cosine=retained_sign*circle.Radius()*
                gp_Vec(axes.XDirection()).Dot(normal);
            const double sine=retained_sign*circle.Radius()*
                gp_Vec(axes.YDirection()).Dot(normal);
            const double amplitude=std::hypot(cosine,sine);
            if(amplitude>boundary_tolerance) {
                const double period=2.*std::numbers::pi;
                const double first=curve.FirstParameter();
                const double last=curve.LastParameter();
                const double base=std::atan2(sine,cosine)+
                    std::numbers::pi;
                const double critical=base+period*std::ceil(
                    (first-base)/period);
                if(critical<=last+1.0e-12)
                    minimum=std::min(minimum,constant-amplitude);
            }
        }
        if(minimum<-boundary_tolerance)return false;
    }
    return true;
}

Cutter make_route_cutter(const std::vector<CutterJob>& jobs,
        std::span<const std::size_t> route,double radius,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        OcctPlanarEdgeFilletEndMode end_mode) {
    if(route.size()<2)
        throw std::invalid_argument(
            "Planar edge Fillet route requires at least two segments");
    for(const auto index:route) {
        if(index>=jobs.size())
            throw std::invalid_argument(
                "Planar edge Fillet route contains an invalid segment");
    }
    const auto endpoints=[](const TopoDS_Edge& edge) {
        std::array<TopoDS_Vertex,2> result;
        TopExp::Vertices(edge,result[0],result[1],true);
        return result;
    };
    const auto common_vertex=[&](const TopoDS_Edge& first,
            const TopoDS_Edge& second) {
        const auto a=endpoints(first),b=endpoints(second);
        TopoDS_Vertex shared;
        for(const auto& first_vertex:a)
            for(const auto& second_vertex:b)
                if(!first_vertex.IsNull()&&
                   first_vertex.IsSame(second_vertex)) {
                    if(!shared.IsNull()&&!shared.IsSame(first_vertex))
                        throw std::invalid_argument(
                            "Planar edge Fillet route segments share more than one endpoint");
                    shared=first_vertex;
                }
        if(shared.IsNull())
            throw std::invalid_argument(
                "Planar edge Fillet route segments are disconnected");
        return shared;
    };

    std::vector<TopoDS_Edge> ordered;
    ordered.reserve(route.size());
    const auto first_shared=common_vertex(jobs[route[0]].edge,
        jobs[route[1]].edge);
    auto first=jobs[route[0]].edge;
    auto first_ends=endpoints(first);
    if(first_ends[0].IsSame(first_shared))first.Reverse();
    ordered.push_back(first);
    auto current=endpoints(ordered.back())[1];
    for(std::size_t position=1;position<route.size();++position) {
        auto edge=jobs[route[position]].edge;
        auto ends=endpoints(edge);
        if(ends[1].IsSame(current))edge.Reverse();
        else if(!ends[0].IsSame(current))
            throw std::invalid_argument(
                "Planar edge Fillet route order is not connected");
        ordered.push_back(edge);
        current=endpoints(ordered.back())[1];
    }

    // The pipe section must belong to the first vertex of the complete open
    // spine.  OCCT accepts a section from an interior route member, but then
    // it can build only the suffix after that member (notably when a small
    // Fillet arc precedes another turn).  Start with the first member's exact
    // local profile; below it is transported from that edge's midpoint to the
    // actual spine start. Terminal extensions are built separately so their
    // clipping planes cannot erase an earlier returning part of the route.
    const auto anchor=route.front();
    auto profile=make_cutter(jobs[anchor].edge,jobs[anchor].first_face,
        jobs[anchor].second_face,radius,tolerance,jobs[anchor].material,
        {},true);

    const auto start_vertex=endpoints(ordered.front())[0];
    const gp_Pnt start_point=BRep_Tool::Pnt(start_vertex);
    const gp_Pnt end_point=BRep_Tool::Pnt(
        endpoints(ordered.back())[1]);
    const auto outward_tangent=[&](const TopoDS_Edge& edge,bool start) {
        const BRepAdaptor_Curve curve(edge);
        gp_Pnt first_point,last_point;
        gp_Vec first_derivative,last_derivative;
        curve.D1(curve.FirstParameter(),first_point,first_derivative);
        curve.D1(curve.LastParameter(),last_point,last_derivative);
        const auto terminal=start?start_point:end_point;
        gp_Vec result;
        if(terminal.Distance(first_point)<=
                terminal.Distance(last_point))
            result=-first_derivative;
        else
            result=last_derivative;
        if(result.SquareMagnitude()<=1.0e-18)
            throw std::invalid_argument(
                "Planar edge Fillet route has a degenerate terminal tangent");
        return gp_Dir(result);
    };
    const gp_Dir start_outward=outward_tangent(ordered.front(),true);
    const gp_Dir end_outward=outward_tangent(ordered.back(),false);
    const auto preceding_point=[&](const TopoDS_Edge& edge,
            const gp_Pnt& terminal) {
        const BRepAdaptor_Curve curve(edge);
        const double first=curve.FirstParameter();
        const double last=curve.LastParameter();
        const bool at_first=terminal.Distance(curve.Value(first))<=
            terminal.Distance(curve.Value(last));
        constexpr double fraction=.1;
        return curve.Value(at_first
            ?first+fraction*(last-first)
            :last-fraction*(last-first));
    };
    const gp_Pnt start_preceding=preceding_point(
        ordered.front(),start_point);
    const gp_Pnt end_preceding=preceding_point(
        ordered.back(),end_point);
    std::array<std::vector<TopoDS_Face>,2> terminal_faces;
    const auto assign_end_face=[&](const TopoDS_Face& face) {
        if(face.IsNull())return;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)
            throw std::invalid_argument(
                "Planar edge Fillet end face must be planar");
        const auto contains=[&](const gp_Pnt& point) {
            if(support.Plane().Distance(point)>tolerance)return false;
            BRepClass_FaceClassifier classifier(face,point,tolerance);
            return classifier.State()==TopAbs_IN||
                classifier.State()==TopAbs_ON;
        };
        const bool first=contains(start_point),last=contains(end_point);
        if(first==last)
            throw std::invalid_argument(
                "Planar edge Fillet end face must contain exactly one route endpoint");
        auto& destination=terminal_faces[first?0:1];
        if(std::ranges::none_of(destination,[&](const auto& existing) {
                return existing.IsSame(face);
            }))destination.push_back(face);
    };
    if(!end_faces.empty())
        for(const auto index:route) {
            assign_end_face(end_faces[index].first);
            assign_end_face(end_faces[index].last);
        }
    const bool explicit_first=!terminal_faces[0].empty();
    const bool explicit_last=!terminal_faces[1].empty();
    const auto& start_job=jobs[route.front()];
    const auto& end_job=jobs[route.back()];
    const bool automatic_request=!profile.adds_material&&end_mode==
        OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope;
    std::array<std::vector<TopoDS_Face>,2> curved_terminal_faces;
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        curved_terminal_faces[0]=local_curved_terminal_faces(
            start_job.material,start_job.first_face,start_job.second_face,
            start_point,tolerance);
        curved_terminal_faces[1]=local_curved_terminal_faces(
            end_job.material,end_job.first_face,end_job.second_face,
            end_point,tolerance);
    }

    // A multi-member route must make the same local curved-terminal decision
    // as the established single-edge path.  Detect the preceding Fillet face
    // before searching for a farther planar cap; otherwise an available remote
    // face can hide the local arc and make one continuous route overrun it.
    const bool local_curved_first=!curved_terminal_faces[0].empty();
    const bool local_curved_last=!curved_terminal_faces[1].empty();
    const bool compound_curved_first=compound_curved_terminal(
        curved_terminal_faces[0],tolerance);
    const bool compound_curved_last=compound_curved_terminal(
        curved_terminal_faces[1],tolerance);
    const auto terminal_inward_directions=[&](const CutterJob& job,
            const gp_Pnt& endpoint) {
        const BRepAdaptor_Curve curve(job.edge);
        const auto path=edge_path(job.edge,tolerance);
        const double parameter=path.middle_parameter;
        gp_Pnt point;gp_Vec derivative;
        curve.D1(parameter,point,derivative);
        if(derivative.SquareMagnitude()<=1.0e-18)
            throw std::invalid_argument(
                "Planar edge Fillet route has a degenerate terminal tangent");
        const gp_Dir tangent(derivative);
        const bool at_first=endpoint.Distance(path.first)<=
            endpoint.Distance(path.last);
        const double transport=(at_first?-.5:.5)*path.length;
        return std::array{
            gp_Dir(transported(path,gp_Vec(inward_direction(job.edge,
                job.first_face,curve,parameter,tangent,tolerance)),
                transport)),
            gp_Dir(transported(path,gp_Vec(inward_direction(job.edge,
                job.second_face,curve,parameter,tangent,tolerance)),
                transport))};
    };
    const auto continues_past_local_curve=[&](const CutterJob& job,
            const gp_Pnt& endpoint,const gp_Dir& outward) {
        const auto inward=terminal_inward_directions(job,endpoint);
        return removal_profile_continues_past_curved_terminal(job.material,
            endpoint,outward,inward[0],inward[1],radius,tolerance);
    };
    // Keep terminal differential work lazy.  Routes which do not meet a
    // preceding curved face retain their established automatic-cap path.
    const bool continue_first=local_curved_first&&!profile.adds_material&&
        !explicit_first&&
        continues_past_local_curve(start_job,start_point,start_outward);
    const bool continue_last=local_curved_last&&!profile.adds_material&&
        !explicit_last&&
        continues_past_local_curve(end_job,end_point,end_outward);
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(continue_first)
            terminal_faces[0]=remote_terminal_faces_through_local_curve(
                start_job.material,start_job.first_face,
                start_job.second_face,start_point,start_outward,tolerance);
        else if(terminal_faces[0].empty()&&
                (!profile.adds_material||!local_curved_first||
                 explicit_first))
            terminal_faces[0]=automatic_terminal_faces(start_job.material,
                start_job.first_face,start_job.second_face,start_point,
                start_outward,start_preceding,tolerance);
        if(continue_last)
            terminal_faces[1]=remote_terminal_faces_through_local_curve(
                end_job.material,end_job.first_face,end_job.second_face,
                end_point,end_outward,tolerance);
        else if(terminal_faces[1].empty()&&
                (!profile.adds_material||!local_curved_last||
                 explicit_last))
            terminal_faces[1]=automatic_terminal_faces(end_job.material,
                end_job.first_face,end_job.second_face,end_point,
                end_outward,end_preceding,tolerance);
    }
    const bool envelope_first=automatic_request&&
        terminal_faces[0].empty()&&
        (!local_curved_first||continue_first)&&
        !has_perpendicular_terminal_face(start_job.material,
            start_job.first_face,start_job.second_face,start_point,
            start_outward,tolerance);
    const bool envelope_last=automatic_request&&
        terminal_faces[1].empty()&&
        (!local_curved_last||continue_last)&&
        !has_perpendicular_terminal_face(end_job.material,
            end_job.first_face,end_job.second_face,end_point,end_outward,
            tolerance);
    const gp_Pnt analytic_start(
        profile.analytic_domain.source_start.x,
        profile.analytic_domain.source_start.y,
        profile.analytic_domain.source_start.z);
    const gp_Pnt analytic_tangent(
        profile.analytic_domain.first_tangent_at_start.x,
        profile.analytic_domain.first_tangent_at_start.y,
        profile.analytic_domain.first_tangent_at_start.z);
    const double cross_span=2.*(analytic_start.Distance(
        analytic_tangent)+radius);
    const auto extension=[&](const auto& faces,const gp_Pnt& endpoint,
            const gp_Dir& outward,const TopoDS_Solid& material,
            bool envelope) {
        if(faces.empty())return envelope
            ?body_envelope_extension(material,cross_span,tolerance):0.;
        double result{};
        for(const auto& face:faces) {
            const BRepAdaptor_Surface support(face,true);
            const auto normal=support.Plane().Axis().Direction();
            const double signed_incidence=normal.Dot(outward);
            const double incidence=std::abs(signed_incidence);
            double distance{};
            if(support.Plane().Distance(endpoint)>tolerance) {
                if(incidence<=1.0e-8)
                    throw std::invalid_argument(
                        "Planar edge Fillet remote end face does not intersect the route");
                distance=gp_Vec(endpoint,support.Plane().Location()).Dot(
                    gp_Vec(normal))/signed_incidence;
                if(distance<=tolerance)
                    throw std::invalid_argument(
                        "Planar edge Fillet remote end face is behind the route endpoint");
            }
            // Tangent terminals need an intentionally overgrown tool.  The
            // independent half-space clips below then establish the exact
            // intersection of all local terminal profiles.
            result=std::max(result,distance+(incidence<=1.0e-8?cross_span:
                cross_span/incidence+tolerance));
        }
        return result;
    };
    double first_extension=extension(terminal_faces[0],start_point,
        start_outward,start_job.material,envelope_first);
    double last_extension=extension(terminal_faces[1],end_point,
        end_outward,end_job.material,envelope_last);
    double first_preview_extension=first_extension;
    double last_preview_extension=last_extension;
    const bool trim_to_curved_first=!continue_first&&!explicit_first&&
        !compound_curved_first&&!curved_terminal_faces[0].empty();
    const bool trim_to_curved_last=!continue_last&&!explicit_last&&
        !compound_curved_last&&!curved_terminal_faces[1].empty();
    std::array<std::vector<TopoDS_Face>,2> curved_terminal_caps;
    if(trim_to_curved_first) {
        curved_terminal_caps[0]=remote_terminal_faces_through_local_curve(
            start_job.material,start_job.first_face,start_job.second_face,
            start_point,start_outward,tolerance);
        // The sweep must reach the cap used to trim the finished tool, while
        // the analytic preview remains local to the selected route endpoint.
        first_preview_extension=std::max(first_preview_extension,cross_span);
        first_extension=std::max(first_extension,cross_span);
        first_extension=std::max(first_extension,extension(
            curved_terminal_caps[0],start_point,start_outward,
            start_job.material,false));
    }
    if(trim_to_curved_last) {
        curved_terminal_caps[1]=remote_terminal_faces_through_local_curve(
            end_job.material,end_job.first_face,end_job.second_face,
            end_point,end_outward,tolerance);
        last_preview_extension=std::max(last_preview_extension,cross_span);
        last_extension=std::max(last_extension,cross_span);
        last_extension=std::max(last_extension,extension(
            curved_terminal_caps[1],end_point,end_outward,
            end_job.material,false));
    }
    auto terminal_clip_faces=terminal_faces;
    for(std::size_t end=0;end<terminal_clip_faces.size();++end)
        for(const auto& cap:curved_terminal_caps[end])
            if(std::ranges::none_of(terminal_clip_faces[end],
                    [&](const auto& face) {return face.IsSame(cap);}))
                terminal_clip_faces[end].push_back(cap);
    profile.analytic_domain.axial_extension=std::max(
        first_preview_extension,last_preview_extension);

    BRepBuilderAPI_MakeWire spine;
    for(const auto& edge:ordered)spine.Add(edge);
    if(!spine.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct one route wire");

    const auto anchor_path=edge_path(jobs[anchor].edge,tolerance);
    const double to_route_start=start_point.Distance(anchor_path.first)<=
            start_point.Distance(anchor_path.last)
        ?-.5*anchor_path.length:.5*anchor_path.length;
    const auto move_profile=[&](Cutter& target,const gp_Trsf& placement) {
        BRepBuilderAPI_Transform moved(target.midpoint_profile,
            placement,true);
        moved.Build();
        if(!moved.IsDone()||moved.Shape().IsNull())
            throw std::runtime_error(
                "Planar edge Fillet could not place its route profile");
        const auto circular=moved.ModifiedShape(
            target.midpoint_circular_edge);
        if(circular.IsNull()||circular.ShapeType()!=TopAbs_EDGE)
            throw std::runtime_error(
                "Planar edge Fillet lost its route profile ancestry");
        target.midpoint_profile=TopoDS::Wire(moved.Shape());
        target.midpoint_circular_edge=TopoDS::Edge(circular);
    };
    if(anchor_path.kind==EdgeFilletPathKind::Linear) {
        gp_Trsf placement;
        placement.SetTranslation(gp_Vec(anchor_path.middle,start_point));
        move_profile(profile,placement);
    } else {
        move_profile(profile,path_rotation(anchor_path,
            to_route_start/anchor_path.path_radius));
    }

    // Corrected Frenet transport is singular when a planar route turns by an
    // exact half revolution: its section can arrive on the return leg rotated
    // by 180 degrees even though every selected edge and the analytical
    // preview retain the same material sides.  Use the normal of one common
    // oriented planar support as a fixed binormal whenever every route member
    // owns a coplanar support face.  This keeps the angular relation to that
    // material face constant through a semicircle.  Mixed/non-planar routes
    // deliberately retain the established corrected-Frenet behaviour.
    const auto planar_binormal=[&]() ->std::optional<gp_Dir> {
        const auto oriented_plane=[](const TopoDS_Face& face)
                ->std::optional<std::pair<gp_Pln,gp_Dir>> {
            if(face.IsNull())return {};
            const BRepAdaptor_Surface surface(face,true);
            if(surface.GetType()!=GeomAbs_Plane)return {};
            auto normal=surface.Plane().Axis().Direction();
            if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            return std::pair{surface.Plane(),normal};
        };
        const auto coplanar=[](const gp_Pln& first,const gp_Pln& second) {
            return first.Axis().Direction().IsParallel(
                       second.Axis().Direction(),1.0e-9)&&
                first.Distance(second.Location())<=1.0e-7;
        };
        const auto& seed=jobs[route.front()];
        for(const auto* candidate:{&seed.first_face,&seed.second_face}) {
            const auto plane=oriented_plane(*candidate);
            if(!plane)continue;
            bool common=true;
            for(const auto index:route) {
                bool found=false;
                for(const auto* face:{&jobs[index].first_face,
                                      &jobs[index].second_face}) {
                    const auto support=oriented_plane(*face);
                    if(support&&coplanar(plane->first,support->first)&&
                       plane->second.Dot(support->second)>1.-1.0e-9) {
                        found=true;break;
                    }
                }
                if(!found) {common=false;break;}
            }
            if(common)return plane->second;
        }
        return {};
    }();
    BRepOffsetAPI_MakePipeShell sweep(spine.Wire());
    if(planar_binormal)sweep.SetMode(*planar_binormal);
    else sweep.SetMode(false);
    sweep.SetTolerance(tolerance,tolerance,1.0e-6);
    sweep.Add(profile.midpoint_profile,start_vertex,false,false);
    sweep.Build();
    if(!sweep.IsDone()||!sweep.MakeSolid()||sweep.Shape().IsNull()||
       !BRepCheck_Analyzer(sweep.Shape()).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet could not sweep one valid route tool");
    std::vector<TopoDS_Face> circular_faces;
    for(const auto& generated:sweep.Generated(
            profile.midpoint_circular_edge))
        if(generated.ShapeType()==TopAbs_FACE)
            circular_faces.push_back(TopoDS::Face(generated));
    // PipeShell can omit earlier faces from Generated(section_edge) when a
    // returning spine contains several analytic transitions. Recover every
    // non-planar lateral face which is not one of the source support
    // surfaces; end caps are planar, while the circular section always
    // produces a curved treatment surface.
    for(TopExp_Explorer item(sweep.Shape(),TopAbs_FACE);
            item.More();item.Next()) {
        const auto candidate=TopoDS::Face(item.Current());
        if(BRepAdaptor_Surface(candidate,true).GetType()==GeomAbs_Plane||
           std::ranges::any_of(route,[&](const auto index) {
               return lies_on_source_surface(candidate,
                          jobs[index].first_face,tolerance)||
                   lies_on_source_surface(candidate,
                          jobs[index].second_face,tolerance);
           })||
           std::ranges::any_of(circular_faces,[&](const auto& current) {
               return current.IsSame(candidate);
           }))continue;
        circular_faces.push_back(candidate);
    }
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet route sweep omitted its circular face");
    profile.solid=sweep.Shape();
    profile.circular_faces=std::move(circular_faces);
    // A terminal plane can be tangent or oblique to the route.  Clipping only
    // the overgrown tail leaves the complete PipeShell section straddling that
    // plane at the authored endpoint.  Trim the base sweep as well whenever
    // the complete selected centre line lies in the retained half-space.  The
    // exact line/circle extrema check deliberately rejects a plane crossed by
    // an earlier returning member, preserving that member while the separate
    // terminal extension below is still clipped.  Multiple safe faces are
    // applied successively, forming their exact half-space intersection.
    const auto clip_base_terminal=[&](
            std::span<const TopoDS_Face> faces,
            const gp_Pnt& retained_point) {
        for(const auto& face:faces)
            if(route_stays_in_retained_halfspace(ordered,face,
                    retained_point,tolerance))
                clip_fillet_tool(profile.solid,profile.circular_faces,
                    face,retained_point,tolerance);
    };
    // An automatic subtractive tool is clipped by the final Body CUT itself.
    // Keeping its complete base sweep also avoids coincident trim faces at a
    // tangent cap.  An explicitly selected end face remains authoritative.
    if(profile.adds_material||explicit_first)
        clip_base_terminal(terminal_clip_faces[0],start_preceding);
    if(profile.adds_material||explicit_last)
        clip_base_terminal(terminal_clip_faces[1],end_preceding);
    // The swept route already reaches the authored endpoint.  A curved
    // terminal can cut into that last section, so clip the base sweep as well
    // as the auxiliary extension.  Clipping only the extension retains the
    // same false flat cap that the extension is meant to replace.
    if(trim_to_curved_first)
        for(const auto& face:curved_terminal_faces[0])
            clip_fillet_tool_to_surface(profile.solid,
                profile.circular_faces,face,start_preceding,tolerance);
    if(trim_to_curved_last)
        for(const auto& face:curved_terminal_faces[1])
            clip_fillet_tool_to_surface(profile.solid,
                profile.circular_faces,face,end_preceding,tolerance);
    const auto add_terminal_extension=[&](const CutterJob& job,
            const gp_Pnt& endpoint,const gp_Dir& outward,double extension,
            std::span<const TopoDS_Face> planar_faces,
            std::span<const TopoDS_Face> curved_faces,
            const gp_Pnt& preceding,bool trim_curved) {
        if(extension<=tolerance)return;
        // A local plane tangent to the route can trim the radial section, but
        // it can never bound a prism extruded along that tangent.  The base
        // PipeShell already ends at the authored route endpoint, so the whole
        // auxiliary tail belongs beyond that endpoint and must be discarded.
        // Oblique and remote terminals still need the tail below to reach
        // their exact intersection.
        if(std::ranges::any_of(planar_faces,[&](const auto& face) {
                const BRepAdaptor_Surface support(face,true);
                return support.GetType()==GeomAbs_Plane&&
                    support.Plane().Distance(endpoint)<=tolerance&&
                    std::abs(support.Plane().Axis().Direction().Dot(
                        outward))<=1.0e-8;
            }))return;
        auto tail=make_cutter(job.edge,job.first_face,job.second_face,
            radius,tolerance,job.material,{},true);
        const auto path=edge_path(job.edge,tolerance);
        const double to_endpoint=endpoint.Distance(path.first)<=
                endpoint.Distance(path.last)
            ?-.5*path.length:.5*path.length;
        if(path.kind==EdgeFilletPathKind::Linear) {
            gp_Trsf placement;
            placement.SetTranslation(gp_Vec(path.middle,endpoint));
            move_profile(tail,placement);
        } else {
            move_profile(tail,path_rotation(path,
                to_endpoint/path.path_radius));
        }
        BRepBuilderAPI_MakeFace section(tail.midpoint_profile);
        if(!section.IsDone())throw std::runtime_error(
            "Planar edge Fillet could not construct its route extension section");
        GProp_GProps section_properties;
        BRepGProp::SurfaceProperties(section.Face(),section_properties);
        const gp_Pnt extension_inside=section_properties.CentreOfMass().
            Translated(gp_Vec(outward)*std::min(extension*.25,radius));
        BRepPrimAPI_MakePrism prism(section.Face(),
            gp_Vec(outward)*extension,false,true);
        prism.Build();
        if(!prism.IsDone()||prism.Shape().IsNull()||
           !BRepCheck_Analyzer(prism.Shape()).IsValid())
            throw std::runtime_error(
                "Planar edge Fillet could not construct a valid route extension");
        tail.solid=prism.Shape();
        tail.circular_faces.clear();
        for(const auto& generated:prism.Generated(
                tail.midpoint_circular_edge))
            if(generated.ShapeType()==TopAbs_FACE)
                tail.circular_faces.push_back(TopoDS::Face(generated));
        if(tail.circular_faces.empty())throw std::runtime_error(
            "Planar edge Fillet route extension omitted its circular face");
        for(const auto& face:planar_faces) {
            clip_fillet_tool(tail.solid,tail.circular_faces,face,
                extension_inside,tolerance);
        }
        if(trim_curved)for(const auto& face:curved_faces)
            clip_fillet_tool_to_surface(tail.solid,tail.circular_faces,
                face,extension_inside,tolerance,&preceding);
        fuse_fillet_tools(profile.solid,profile.circular_faces,
            tail.solid,tail.circular_faces,tolerance);
    };
    add_terminal_extension(start_job,start_point,start_outward,
        first_extension,terminal_clip_faces[0],curved_terminal_faces[0],
        start_preceding,trim_to_curved_first);
    add_terminal_extension(end_job,end_point,end_outward,last_extension,
        terminal_clip_faces[1],curved_terminal_faces[1],end_preceding,
        trim_to_curved_last);
    profile.source_jobs.assign(route.begin(),route.end());
    return profile;
}

} // namespace

OcctPlanarEdgeFilletResult occt_planar_edge_fillet(
        const TopoDS_Shape& input,std::span<const TopoDS_Edge> edges,
        double radius,double tolerance,
        std::span<const OcctPlanarEdgeEndFaces> end_faces,
        OcctPlanarEdgeFilletEndMode end_mode,
        std::span<const std::size_t> route_edge_counts,
        std::span<const OcctPlanarEdgeSupport> supports) {
    if(input.IsNull()||edges.empty())
        throw std::invalid_argument(
            "Planar edge Fillet requires a Body and selected edges");
    if(!std::isfinite(radius)||radius<=0.||
       !std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Planar edge Fillet radius and tolerance must be positive");
    if(!end_faces.empty()&&end_faces.size()!=edges.size())
        throw std::invalid_argument(
            "Planar edge Fillet end faces must be parallel to selected edges");
    if(!route_edge_counts.empty()&&
       std::accumulate(route_edge_counts.begin(),route_edge_counts.end(),
           std::size_t{})!=edges.size())
        throw std::invalid_argument(
            "Planar edge Fillet route sizes do not match selected edges");
    if(!supports.empty()&&supports.size()!=edges.size())
        throw std::invalid_argument(
            "Planar edge Fillet supports must be parallel to selected edges");
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    TopTools_IndexedDataMapOfShapeListOfShape edge_solids;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_SOLID,
        edge_solids);
    TopTools_IndexedMapOfShape requested_edges;
    const auto preparation_started=Clock::now();
    std::vector<CutterJob> jobs;
    jobs.reserve(edges.size());
    for(std::size_t requested_index=0;requested_index<edges.size();
            ++requested_index) {
        const auto& edge=edges[requested_index];
        if(requested_edges.Contains(edge))
            throw std::invalid_argument(
                "Planar edge Fillet received the same edge more than once");
        requested_edges.Add(edge);
        std::vector<TopoDS_Face> faces;
        std::vector<TopoDS_Solid> solids;
        const bool exact_source=!supports.empty()&&
            !supports[requested_index].material.IsNull();
        if(exact_source) {
            const auto& support=supports[requested_index];
            if(support.first.IsNull()||support.second.IsNull()||
               support.first.IsSame(support.second))
                throw std::invalid_argument(
                    "Planar edge Fillet original support is invalid");
            TopTools_IndexedMapOfShape first_edges,second_edges,
                material_edges;
            TopExp::MapShapes(support.first,TopAbs_EDGE,first_edges);
            TopExp::MapShapes(support.second,TopAbs_EDGE,second_edges);
            TopExp::MapShapes(support.material,TopAbs_EDGE,material_edges);
            if(!first_edges.Contains(edge)||!second_edges.Contains(edge)||
               !material_edges.Contains(edge))
                throw std::invalid_argument(
                    "Planar edge Fillet original support does not contain its edge");
            faces={support.first,support.second};
            solids={support.material};
        } else {
            const int face_index=edge_faces.FindIndex(edge);
            const int solid_index=edge_solids.FindIndex(edge);
            if(!face_index||!solid_index)
                throw std::invalid_argument(
                    "Planar edge Fillet edge is absent from the input Body");
            for(const auto& value:edge_faces.FindFromIndex(face_index)) {
                const auto face=TopoDS::Face(value);
                if(std::ranges::none_of(faces,[&](const auto& current) {
                        return current.IsSame(face);
                    }))faces.push_back(face);
            }
            for(const auto& value:edge_solids.FindFromIndex(solid_index)) {
                const auto solid=TopoDS::Solid(value);
                if(std::ranges::none_of(solids,[&](const auto& current) {
                        return current.IsSame(solid);
                    }))solids.push_back(solid);
            }
        }
        if(faces.size()!=2||solids.size()!=1)
            throw std::invalid_argument(
                "Planar edge Fillet requires one manifold solid edge");
        jobs.push_back({edge,faces[0],faces[1],solids.front()});
    }
    std::vector<std::vector<std::size_t>> routes;
    if(route_edge_counts.empty()) {
        routes.reserve(jobs.size());
        for(std::size_t index=0;index<jobs.size();++index) {
            routes.push_back({index});
        }
    } else {
        routes.reserve(route_edge_counts.size());
        std::size_t offset{};
        for(const auto count:route_edge_counts) {
            if(count==0)
                throw std::invalid_argument(
                    "Planar edge Fillet route cannot be empty");
            auto& route=routes.emplace_back();
            route.reserve(count);
            for(std::size_t index=0;index<count;++index) {
                route.push_back(offset+index);
            }
            offset+=count;
        }
    }
    const auto smooth_fragment=[&](std::size_t index) {
        const auto& job=jobs.at(index);
        const BRepAdaptor_Curve curve(job.edge);
        const double length=GCPnts_AbscissaPoint::Length(curve,
            curve.FirstParameter(),curve.LastParameter(),tolerance);
        if(!std::isfinite(length)||length<=tolerance)return false;
        GCPnts_AbscissaPoint midpoint(tolerance,curve,length/2.,
            curve.FirstParameter());
        if(!midpoint.IsDone())return false;
        const double parameter=midpoint.Parameter();
        gp_Pnt point;gp_Vec derivative;
        curve.D1(parameter,point,derivative);
        if(derivative.SquareMagnitude()<=1.0e-18)return false;
        const gp_Dir tangent(derivative);
        const auto first=inward_direction(job.edge,job.first_face,curve,
            parameter,tangent,tolerance);
        const auto second=inward_direction(job.edge,job.second_face,curve,
            parameter,tangent,tolerance);
        return first.Angle(second)>=std::numbers::pi-1.0e-6;
    };
    const auto connected=[&](std::size_t first,std::size_t second) {
        TopTools_IndexedMapOfShape first_vertices,second_vertices;
        TopExp::MapShapes(jobs[first].edge,TopAbs_VERTEX,first_vertices);
        TopExp::MapShapes(jobs[second].edge,TopAbs_VERTEX,second_vertices);
        for(int index=1;index<=first_vertices.Extent();++index)
            if(second_vertices.Contains(first_vertices.FindKey(index)))
                return true;
        return false;
    };
    std::vector<std::vector<std::size_t>> active_routes;
    active_routes.reserve(routes.size());
    for(const auto& route:routes) {
        const bool has_active=std::ranges::any_of(route,
            [&](const auto index){return !smooth_fragment(index);});
        if(!has_active) {
            active_routes.push_back(route);
            continue;
        }
        std::vector<std::size_t> current;
        for(const auto index:route) {
            if(smooth_fragment(index)) {
                if(!current.empty()) {
                    active_routes.push_back(std::move(current));
                    current.clear();
                }
                continue;
            }
            if(!current.empty()&&!connected(current.back(),index)) {
                active_routes.push_back(std::move(current));
                current.clear();
            }
            current.push_back(index);
        }
        if(!current.empty())active_routes.push_back(std::move(current));
    }
    routes=std::move(active_routes);
    const auto edge_vertices=[](const TopoDS_Edge& edge) {
        std::array<TopoDS_Vertex,2> result;
        TopExp::Vertices(edge,result[0],result[1],true);
        if(result[0].IsNull()||result[1].IsNull())
            throw std::invalid_argument(
                "Planar edge Fillet route has no finite endpoints");
        return result;
    };
    const auto route_terminal_vertices=[&](
            const std::vector<std::size_t>& route) {
        if(route.empty())
            throw std::invalid_argument(
                "Planar edge Fillet route cannot be empty");
        if(route.size()==1)return edge_vertices(jobs[route.front()].edge);
        const auto terminal=[&](std::size_t job,std::size_t neighbour) {
            const auto current=edge_vertices(jobs[job].edge);
            const auto adjacent=edge_vertices(jobs[neighbour].edge);
            const bool first_shared=current[0].IsSame(adjacent[0])||
                current[0].IsSame(adjacent[1]);
            const bool last_shared=current[1].IsSame(adjacent[0])||
                current[1].IsSame(adjacent[1]);
            if(first_shared==last_shared)
                throw std::invalid_argument(
                    "Planar edge Fillet route has an ambiguous endpoint");
            return first_shared?current[1]:current[0];
        };
        return std::array{
            terminal(route.front(),route[1]),
            terminal(route.back(),route[route.size()-2])};
    };
    std::vector<std::optional<bool>> route_merge_sense(jobs.size());
    const auto treatment_adds_material=[&](std::size_t job_index) {
        if(!route_merge_sense[job_index]) {
            const auto& job=jobs[job_index];
            const BRepAdaptor_Curve curve(job.edge);
            if(curve.GetType()==GeomAbs_Line||
               curve.GetType()==GeomAbs_Circle) {
                route_merge_sense[job_index]=make_cutter(
                    job.edge,job.first_face,job.second_face,radius,
                    tolerance,job.material,{},true).adds_material;
            } else {
                const double length=GCPnts_AbscissaPoint::Length(curve,
                    curve.FirstParameter(),curve.LastParameter(),tolerance);
                if(!std::isfinite(length)||length<=tolerance)
                    throw std::invalid_argument(
                        "Planar edge Fillet route member is too short");
                GCPnts_AbscissaPoint midpoint(tolerance,curve,length/2.,
                    curve.FirstParameter());
                if(!midpoint.IsDone())
                    throw std::invalid_argument(
                        "Planar edge Fillet could not locate the route member midpoint");
                gp_Pnt point;gp_Vec derivative;
                curve.D1(midpoint.Parameter(),point,derivative);
                if(derivative.SquareMagnitude()<=1.0e-18)
                    throw std::invalid_argument(
                        "Planar edge Fillet route has a degenerate tangent");
                const gp_Dir tangent(derivative);
                const auto first=inward_direction(job.edge,job.first_face,
                    curve,midpoint.Parameter(),tangent,tolerance);
                const auto second=inward_direction(job.edge,job.second_face,
                    curve,midpoint.Parameter(),tangent,tolerance);
                gp_Vec bisector=gp_Vec(first)+gp_Vec(second);
                if(bisector.SquareMagnitude()<=1.0e-18)
                    throw std::invalid_argument(
                        "Planar edge Fillet route has no finite material wedge");
                bisector.Normalize();
                const double probe=std::min(radius*.1,
                    std::max(10.*tolerance,length*1.0e-5));
                BRepClass3d_SolidClassifier wedge(job.material,
                    point.Translated(bisector*probe),tolerance);
                BRepClass3d_SolidClassifier opposite(job.material,
                    point.Translated(-bisector*probe),tolerance);
                const bool removes=wedge.State()==TopAbs_IN&&
                    opposite.State()==TopAbs_OUT;
                const bool adds=wedge.State()==TopAbs_OUT&&
                    opposite.State()==TopAbs_IN;
                if(!removes&&!adds)
                    throw std::invalid_argument(
                        "Planar edge Fillet could not classify the route material wedge");
                route_merge_sense[job_index]=adds;
            }
        }
        return *route_merge_sense[job_index];
    };
    // A click on a current straight edge and a separate click on the current
    // tangent edge produced by an earlier treatment are persisted as two
    // selection items.  Geometrically they are one continuous rolling path,
    // so join only an unambiguous pair of route ends with equal material
    // sense.  A non-tangent shared vertex remains a real node and is handled
    // by the node connector below.
    bool merged_route{};
    do {
        merged_route=false;
        for(std::size_t first=0;first<routes.size()&&!merged_route;++first) {
            const auto first_ends=route_terminal_vertices(routes[first]);
            for(std::size_t second=first+1;
                    second<routes.size()&&!merged_route;++second) {
                const auto second_ends=route_terminal_vertices(routes[second]);
                for(std::size_t first_end=0;
                        first_end<2&&!merged_route;++first_end)
                    for(std::size_t second_end=0;
                            second_end<2&&!merged_route;++second_end) {
                        if(!first_ends[first_end].IsSame(
                                second_ends[second_end]))continue;
                        std::size_t incidence{};
                        for(const auto& route:routes)
                            for(const auto& endpoint:
                                    route_terminal_vertices(route))
                                if(endpoint.IsSame(first_ends[first_end]))
                                    ++incidence;
                        if(incidence!=2)continue;
                        const auto first_job=first_end==0
                            ?routes[first].front():routes[first].back();
                        const auto second_job=second_end==0
                            ?routes[second].front():routes[second].back();
                        if(!jobs[first_job].material.IsSame(
                                jobs[second_job].material)||
                           treatment_adds_material(first_job)!=
                                treatment_adds_material(second_job))continue;
                        const auto first_tangent=node_outgoing_tangent(
                            jobs[first_job].edge,first_ends[first_end],
                            tolerance);
                        const auto second_tangent=node_outgoing_tangent(
                            jobs[second_job].edge,second_ends[second_end],
                            tolerance);
                        if(!first_tangent.IsParallel(second_tangent,
                                std::max(1.0e-9,tolerance)))continue;
                        if(first_end==0)
                            std::ranges::reverse(routes[first]);
                        if(second_end==1)
                            std::ranges::reverse(routes[second]);
                        routes[first].insert(routes[first].end(),
                            routes[second].begin(),routes[second].end());
                        routes.erase(routes.begin()+second);
                        merged_route=true;
                    }
            }
        }
    } while(merged_route);
    std::vector<std::vector<SharedEndpoint>> route_endpoints;
    route_endpoints.reserve(routes.size());
    for(const auto& route:routes) {
        auto& endpoints=route_endpoints.emplace_back();
        for(const auto job_index:route) {
            TopTools_IndexedMapOfShape edge_vertices;
            TopExp::MapShapes(jobs[job_index].edge,TopAbs_VERTEX,
                edge_vertices);
            for(int index=1;index<=edge_vertices.Extent();++index) {
                const auto vertex=TopoDS::Vertex(
                    edge_vertices.FindKey(index));
                auto existing=std::ranges::find_if(endpoints,
                    [&](const auto& item) {
                        return item.vertex.IsSame(vertex);
                    });
                if(existing==endpoints.end())
                    endpoints.push_back({vertex,{job_index}});
                else {
                    existing->jobs.push_back(job_index);
                    if(existing->jobs.size()>2)
                        throw std::invalid_argument(
                            "Planar edge Fillet endpoint belongs to more than two fragments of one route");
                }
            }
        }
    }
    std::vector<SharedEndpoint> selected_nodes;
    std::vector<bool> active_jobs(jobs.size());
    for(const auto& route:routes)
        for(const auto job:route)active_jobs[job]=true;
    for(std::size_t job=0;job<jobs.size();++job) {
        // A G1 boundary fragment removed from the active route has no material
        // wedge and therefore owns neither a tool nor a node branch.  Keeping
        // it in the node incidence graph would demand a signed tool which is
        // intentionally absent and reject an otherwise valid connected path.
        if(!active_jobs[job])continue;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(jobs[job].edge,TopAbs_VERTEX,vertices);
        for(int index=1;index<=vertices.Extent();++index) {
            const auto vertex=TopoDS::Vertex(vertices.FindKey(index));
            const auto found=std::ranges::find_if(selected_nodes,
                [&](const auto& node) {
                    return node.vertex.IsSame(vertex);
                });
            if(found==selected_nodes.end())
                selected_nodes.push_back({vertex,{job}});
            else
                found->jobs.push_back(job);
        }
    }
    struct NodeTerminalAxis {
        gp_Pnt center;
        gp_Dir outgoing;
        double extent{};
        bool at_first{};
    };
    struct NodeMiterTrim {
        TopoDS_Face plane;
        gp_Pnt retained_point;
    };
    std::vector<CutterEndExtensions> job_node_extensions(jobs.size());
    std::vector<std::vector<NodeMiterTrim>> job_node_miter_trims(
        jobs.size());
    std::vector<bool> job_is_single_route(jobs.size());
    for(const auto& route:routes)
        if(route.size()==1)job_is_single_route[route.front()]=true;
    const auto planar_job=[](const CutterJob& job) {
        return BRepAdaptor_Surface(job.first_face,true).GetType()==
                GeomAbs_Plane&&
            BRepAdaptor_Surface(job.second_face,true).GetType()==
                GeomAbs_Plane;
    };
    const auto terminal_axis=[&](std::size_t job_index,
            const TopoDS_Vertex& vertex,const Cutter& profile)
            ->std::optional<NodeTerminalAxis> {
        const auto& domain=profile.analytic_domain;
        if(domain.path_kind!=EdgeFilletPathKind::Linear)return {};
        const gp_Pnt source_start(domain.source_start.x,
            domain.source_start.y,domain.source_start.z);
        const gp_Pnt source_end(domain.source_end.x,
            domain.source_end.y,domain.source_end.z);
        const auto node=BRep_Tool::Pnt(vertex);
        const bool at_first=node.Distance(source_start)<=
            node.Distance(source_end);
        if(node.Distance(at_first?source_start:source_end)>10.*tolerance)
            return {};
        gp_Pnt center(domain.center_at_start.x,
            domain.center_at_start.y,domain.center_at_start.z);
        if(!at_first)center.Translate(gp_Vec(source_start,source_end));
        return NodeTerminalAxis{center,node_outgoing_tangent(
            jobs[job_index].edge,vertex,tolerance),
            source_start.Distance(source_end),at_first};
    };
    const auto set_node_extension=[](CutterEndExtensions& target,
            const NodeTerminalAxis& terminal,double value) {
        auto& destination=terminal.at_first?target.first:target.last;
        destination=std::max(destination,value);
    };
    // Two independent plane/plane branches meet where their rolling-cylinder
    // axes intersect. Extend each real branch only to that intersection and
    // trim both tools by their equal-distance bisector plane. The complete
    // circular end profile therefore owns a real trimming surface instead of
    // being hidden later by an overlapping auxiliary solid. The construction
    // is angle-independent and also covers non-right planar corners.
    for(const auto& node:selected_nodes) {
        if(node.jobs.size()!=2)continue;
        const auto first_job=node.jobs[0];
        const auto second_job=node.jobs[1];
        if(!job_is_single_route[first_job]||
           !job_is_single_route[second_job])continue;
        const auto& first=jobs[first_job];
        const auto& second=jobs[second_job];
        if(!first.material.IsSame(second.material)||
           !planar_job(first)||!planar_job(second)||
           node_outgoing_tangent(first.edge,node.vertex,tolerance).
               IsParallel(node_outgoing_tangent(second.edge,node.vertex,
                   tolerance),std::max(1.0e-9,tolerance)))continue;
        if(!end_faces.empty()&&
           (!end_faces[first_job].first.IsNull()||
            !end_faces[first_job].last.IsNull()||
            !end_faces[second_job].first.IsNull()||
            !end_faces[second_job].last.IsNull()))continue;
        auto first_profile=make_cutter(first.edge,first.first_face,
            first.second_face,radius,tolerance,first.material,{},true);
        auto second_profile=make_cutter(second.edge,second.first_face,
            second.second_face,radius,tolerance,second.material,{},true);
        if(first_profile.adds_material!=second_profile.adds_material)
            continue;
        const auto first_terminal=terminal_axis(first_job,node.vertex,
            first_profile);
        const auto second_terminal=terminal_axis(second_job,node.vertex,
            second_profile);
        if(!first_terminal||!second_terminal)continue;
        const gp_Vec first_direction(first_terminal->outgoing);
        const gp_Vec second_direction(second_terminal->outgoing);
        const gp_Vec separation(second_terminal->center,
            first_terminal->center);
        const double cosine=first_direction.Dot(second_direction);
        const double denominator=1.-cosine*cosine;
        if(denominator<=1.0e-10)continue;
        const double first_projection=first_direction.Dot(separation);
        const double second_projection=second_direction.Dot(separation);
        const double first_parameter=(cosine*second_projection-
            first_projection)/denominator;
        const double second_parameter=(second_projection-
            cosine*first_projection)/denominator;
        const auto first_closest=first_terminal->center.Translated(
            first_direction*first_parameter);
        const auto second_closest=second_terminal->center.Translated(
            second_direction*second_parameter);
        const double axis_tolerance=std::max(10.*tolerance,
            radius*1.0e-7);
        if(first_closest.Distance(second_closest)>axis_tolerance||
           first_parameter>first_terminal->extent+axis_tolerance||
           second_parameter>second_terminal->extent+axis_tolerance)
            continue;
        const double first_extension=std::max(0.,-first_parameter);
        const double second_extension=std::max(0.,-second_parameter);
        if(first_extension<=tolerance||second_extension<=tolerance)
            continue;
        const auto node_point=BRep_Tool::Pnt(node.vertex);
        const double extension_limit=2.*(radius+std::max(
            node_point.Distance(first_terminal->center),
            node_point.Distance(second_terminal->center)));
        if(first_extension>extension_limit||
           second_extension>extension_limit)continue;
        gp_Vec miter_normal(first_terminal->outgoing);
        miter_normal-=gp_Vec(second_terminal->outgoing);
        if(miter_normal.SquareMagnitude()<=1.0e-18)continue;
        const gp_Pnt axis_intersection(
            .5*(first_closest.X()+second_closest.X()),
            .5*(first_closest.Y()+second_closest.Y()),
            .5*(first_closest.Z()+second_closest.Z()));
        BRepBuilderAPI_MakeFace miter_builder(
            gp_Pln(axis_intersection,gp_Dir(miter_normal)));
        if(!miter_builder.IsDone()||miter_builder.Face().IsNull())
            continue;
        set_node_extension(job_node_extensions[first_job],
            *first_terminal,first_extension);
        set_node_extension(job_node_extensions[second_job],
            *second_terminal,second_extension);
        const double retained_distance=std::max(radius,
            std::max(first_extension,second_extension));
        job_node_miter_trims[first_job].push_back({
            miter_builder.Face(),axis_intersection.Translated(
                retained_distance*first_direction)});
        job_node_miter_trims[second_job].push_back({
            miter_builder.Face(),axis_intersection.Translated(
                retained_distance*second_direction)});
    }
    std::vector<Cutter> cutters;
    cutters.reserve(routes.size());
    for(const auto& route:routes) {
        if(route.size()>1) {
            cutters.push_back(make_route_cutter(jobs,route,radius,
                tolerance,end_faces,end_mode));
            continue;
        }
        const auto index=route.front();
        const auto& job=jobs[index];
        auto cutter=make_cutter(job.edge,job.first_face,job.second_face,
            radius,tolerance,job.material,
            end_faces.empty()?OcctPlanarEdgeEndFaces{}:end_faces[index],
            false,end_mode,job_node_extensions[index]);
        for(const auto& trim:job_node_miter_trims[index])
            clip_fillet_tool(cutter.solid,cutter.circular_faces,
                trim.plane,trim.retained_point,tolerance);
        cutter.source_jobs={index};
        cutters.push_back(std::move(cutter));
    }
    // Every selected fragment owns its exact tool.  Adjacent fragments may
    // change support faces and CUT/FUSE sense at their stable shared vertex;
    // forcing equal endpoint profiles would incorrectly reject a legitimate
    // line-radius-line transition.  The ordered Booleans and the final body
    // validity check below remain the durable acceptance boundary.
    for(const auto& endpoints:route_endpoints) {
        for(const auto& endpoint:endpoints) {
            if(endpoint.jobs.size()!=2)continue;
            const auto shared=BRep_Tool::Pnt(endpoint.vertex);
            const auto tangent_at_shared=[&](std::size_t job_index) {
                const BRepAdaptor_Curve curve(jobs[job_index].edge);
                const auto first_parameter=curve.FirstParameter();
                const auto last_parameter=curve.LastParameter();
                const auto first=curve.Value(first_parameter);
                const auto last=curve.Value(last_parameter);
                const auto parameter=shared.Distance(first)<=
                        shared.Distance(last)
                    ?first_parameter:last_parameter;
                gp_Pnt point;
                gp_Vec derivative;
                curve.D1(parameter,point,derivative);
                if(point.Distance(shared)>tolerance||
                   derivative.SquareMagnitude()<=1.0e-18)
                    throw std::invalid_argument(
                        "Planar edge Fillet route has no finite terminal tangent");
                return gp_Dir(derivative);
            };
            const auto first=tangent_at_shared(endpoint.jobs[0]);
            const auto second=tangent_at_shared(endpoint.jobs[1]);
            const double angular_tolerance=std::max(1.0e-9,
                tolerance/std::max(1.,radius));
            if(!first.IsParallel(second,angular_tolerance))
                throw std::invalid_argument(
                    "Planar edge Fillet fragments are not tangent at their shared endpoint");
        }
    }
    std::vector<std::optional<bool>> job_adds_material(jobs.size());
    for(const auto& cutter:cutters)
        for(const auto job:cutter.source_jobs) {
            if(job_adds_material[job]&&
                    *job_adds_material[job]!=cutter.adds_material)
                throw std::invalid_argument(
                    "ZIMA Fillet route changes material sense");
            job_adds_material[job]=cutter.adds_material;
        }
    std::vector<NodeConnector> node_connectors;
    const double node_angular_tolerance=std::max(1.0e-9,tolerance);
    for(const auto& node:selected_nodes) {
        if(node.jobs.size()<2)continue;
        if(node.jobs.size()>3)
            throw std::invalid_argument(
                "ZIMA Fillet supports only two- or three-branch nodes");
        std::vector<gp_Dir> directions;
        directions.reserve(node.jobs.size());
        for(const auto job:node.jobs)
            directions.push_back(node_outgoing_tangent(
                jobs[job].edge,node.vertex,tolerance));
        const bool one_line=node.jobs.size()==2&&
            directions.front().IsParallel(directions.back(),
                node_angular_tolerance);
        if(one_line)continue;
        if(std::ranges::any_of(node.jobs,[&](const auto job) {
                return !job_adds_material[job].has_value();
            }))
            throw std::invalid_argument(
                "ZIMA Fillet node has no signed branch tool");
        const bool adds_material=
            *job_adds_material[node.jobs.front()];
        if(std::ranges::any_of(node.jobs,[&](const auto job) {
                return *job_adds_material[job]!=adds_material;
            }))
            throw std::invalid_argument(
                "ZIMA Fillet node mixes inner and outer branches");
        if(node.jobs.size()==3)
            node_connectors.push_back(
                make_fillet_corner_connector(node.vertex,
                    {directions[0],directions[1],directions[2]},
                    radius,tolerance,node.jobs,adds_material));
        else {
            const auto& material=jobs[node.jobs.front()].material;
            if(std::ranges::any_of(node.jobs,[&](const auto job) {
                    return !jobs[job].material.IsSame(material);
                }))
                throw std::invalid_argument(
                    "ZIMA Fillet node spans more than one material solid");
            // Qualifying two-route plane/plane nodes were extended and mitered
            // directly above. Other endpoint arrangements retain their
            // perpendicular circular ends.
        }
    }
    OcctPlanarEdgeFilletResult result;
    result.metrics.requested_edges=edges.size();
    result.metrics.cutter_solids=cutters.size();
    result.removal_domains.reserve(jobs.size());
    result.analytic_removal_domains.reserve(jobs.size());
    for(const auto& cutter:cutters) {
        for(const auto source_job:cutter.source_jobs) {
            static_cast<void>(source_job);
            result.removal_domains.push_back(cutter.solid);
            result.analytic_removal_domains.push_back(
                cutter.analytic_domain);
        }
    }
    result.metrics.preparation_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-preparation_started).count();

    const auto boolean_started=Clock::now();
    TopoDS_Shape current=input;
    const auto apply_boolean=[&](auto& operation,std::size_t first,
            std::size_t last) {
        NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(current);
        for(std::size_t index=first;index<last;++index)
            tools.Append(cutters[index].solid);
        operation.SetArguments(arguments);operation.SetTools(tools);
        operation.SetNonDestructive(true);operation.SetRunParallel(false);
        operation.SetToFillHistory(true);operation.SetFuzzyValue(tolerance);
        operation.Build();
        if(!operation.IsDone()||operation.Shape().IsNull())
            throw std::runtime_error(
                "Planar edge Fillet Boolean failed");
        auto stage_history=operation.History();
        if(!stage_history)
            throw std::runtime_error(
                "Planar edge Fillet Boolean omitted topology history");
        if(result.history)result.history->Merge(stage_history);
        else result.history=std::move(stage_history);
        current=operation.Shape();
    };
    // Preserve authored edge order. Consecutive edges with the same material
    // sense share one Boolean for the normal fast path; a mixed route changes
    // operation only where its authored CUT/FUSE sense changes.
    for(std::size_t first=0;first<cutters.size();) {
        std::size_t last=first+1;
        while(last<cutters.size()&&
              cutters[last].adds_material==cutters[first].adds_material)
            ++last;
        if(cutters[first].adds_material) {
            BRepAlgoAPI_Fuse fuse;apply_boolean(fuse,first,last);
        } else {
            BRepAlgoAPI_Cut cut;apply_boolean(cut,first,last);
        }
        first=last;
    }
    // Three bands use the established exact local corner connector. Two-band
    // nodes are already represented by their extended and miter-trimmed tools.
    for(const auto& connector:node_connectors) {
        NCollection_List<TopoDS_Shape> arguments,tools;
        arguments.Append(current);tools.Append(connector.solid);
        if(connector.adds_material) {
            BRepAlgoAPI_Fuse fuse;
            fuse.SetArguments(arguments);fuse.SetTools(tools);
            fuse.SetNonDestructive(true);fuse.SetRunParallel(false);
            fuse.SetToFillHistory(true);fuse.SetFuzzyValue(tolerance);
            fuse.Build();
            if(!fuse.IsDone()||fuse.Shape().IsNull()||!fuse.History())
                throw std::runtime_error(
                    "ZIMA Fillet node Boolean failed");
            result.history->Merge(fuse.History());
            current=fuse.Shape();
        } else {
            BRepAlgoAPI_Cut cut;
            cut.SetArguments(arguments);cut.SetTools(tools);
            cut.SetNonDestructive(true);cut.SetRunParallel(false);
            cut.SetToFillHistory(true);cut.SetFuzzyValue(tolerance);
            cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull()||!cut.History())
                throw std::runtime_error(
                    "ZIMA Fillet node Boolean failed");
            result.history->Merge(cut.History());
            current=cut.Shape();
        }
    }
    const bool has_general_route=std::ranges::any_of(routes,
        [&](const auto& route) {
            return route.size()>1&&std::ranges::any_of(route,
                [&](const auto job) {
                    const auto type=BRepAdaptor_Curve(
                        jobs[job].edge).GetType();
                    return type!=GeomAbs_Line&&type!=GeomAbs_Circle;
                });
        });
    // The exact arc-and-two-lines profile can leave a small continuation on a
    // periodic Body surface.  ShapeUpgrade normally starts from the complete
    // periodic face and can then fail to relocate the continuation across its
    // U seam.  Unify that new local pair with the smaller continuation as the
    // reference face before the general same-domain cleanup.  This changes
    // only a new seam on one analytical periodic surface and preserves its
    // replacement history for downstream references.
    TopTools_IndexedMapOfShape input_edges;
    TopExp::MapShapes(input,TopAbs_EDGE,input_edges);
    const auto is_original_boundary=[&](const TopoDS_Edge& edge) {
        return std::ranges::any_of(
            std::views::iota(1,input_edges.Extent()+1),
            [&](int input_index) {
                const auto candidate=TopoDS::Edge(
                    input_edges.FindKey(input_index));
                if(!BRepTools::Compare(edge,candidate))return false;
                TopoDS_Vertex first_a,first_b,second_a,second_b;
                TopExp::Vertices(edge,first_a,first_b);
                TopExp::Vertices(candidate,second_a,second_b);
                if(first_a.IsNull()||first_b.IsNull()||
                   second_a.IsNull()||second_b.IsNull())
                    return first_a.IsNull()==second_a.IsNull()&&
                        first_b.IsNull()==second_b.IsNull();
                const auto a=BRep_Tool::Pnt(first_a);
                const auto b=BRep_Tool::Pnt(first_b);
                const auto c=BRep_Tool::Pnt(second_a);
                const auto d=BRep_Tool::Pnt(second_b);
                return (a.Distance(c)<=tolerance&&
                        b.Distance(d)<=tolerance)||
                    (a.Distance(d)<=tolerance&&
                        b.Distance(c)<=tolerance);
            });
    };
    const auto merge_periodic_continuation=[&]() {
        TopTools_IndexedMapOfShape current_faces,current_edges;
        TopExp::MapShapes(current,TopAbs_FACE,current_faces);
        TopExp::MapShapes(current,TopAbs_EDGE,current_edges);
        TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
        TopExp::MapShapesAndAncestors(current,TopAbs_EDGE,TopAbs_FACE,
            edge_faces);
        for(int index=1;index<=current_edges.Extent();++index) {
            const auto edge=TopoDS::Edge(current_edges.FindKey(index));
            if(is_original_boundary(edge))continue;
            const int edge_index=edge_faces.FindIndex(edge);
            if(!edge_index)continue;
            const auto& adjacent=edge_faces.FindFromIndex(edge_index);
            if(adjacent.Extent()!=2)continue;
            auto iterator=adjacent.cbegin();
            auto first_face=TopoDS::Face(*iterator);++iterator;
            auto second_face=TopoDS::Face(*iterator);
            if(first_face.IsSame(second_face)||
               first_face.Orientation()!=second_face.Orientation()||
               !same_curved_terminal_surface(first_face,second_face,
                   tolerance))continue;
            const auto first_geometry=BRep_Tool::Surface(first_face);
            const auto second_geometry=BRep_Tool::Surface(second_face);
            if((first_geometry.IsNull()||
                (!first_geometry->IsUPeriodic()&&
                 !first_geometry->IsVPeriodic()))&&
               (second_geometry.IsNull()||
                (!second_geometry->IsUPeriodic()&&
                 !second_geometry->IsVPeriodic())))continue;
            GProp_GProps first_properties,second_properties;
            BRepGProp::SurfaceProperties(first_face,first_properties);
            BRepGProp::SurfaceProperties(second_face,second_properties);
            if(first_properties.Mass()>second_properties.Mass())
                std::swap(first_face,second_face);
            BRep_Builder builder;
            TopoDS_Compound pair;
            builder.MakeCompound(pair);
            builder.Add(pair,first_face);
            builder.Add(pair,second_face);
            ShapeUpgrade_UnifySameDomain pair_cleanup(
                pair,false,true,false);
            pair_cleanup.SetLinearTolerance(tolerance);
            pair_cleanup.SetAngularTolerance(1.0e-9);
            pair_cleanup.Build();
            TopTools_IndexedMapOfShape merged_faces;
            TopExp::MapShapes(pair_cleanup.Shape(),TopAbs_FACE,
                merged_faces);
            if(merged_faces.Extent()!=1)continue;
            TopTools_ListOfShape parts;
            parts.Append(first_face);parts.Append(second_face);
            occ::handle<ShapeBuild_ReShape> reshape=
                new ShapeBuild_ReShape;
            reshape->Merge(parts,merged_faces.FindKey(1));
            const auto merged=reshape->Apply(current);
            if(merged.IsNull()||
               !BRepCheck_Analyzer(merged,true,true,false).IsValid())
                continue;
            TopTools_IndexedMapOfShape replacement_faces;
            TopExp::MapShapes(merged,TopAbs_FACE,replacement_faces);
            if(replacement_faces.Extent()>=current_faces.Extent())continue;
            if(const auto history=reshape->History();!history.IsNull())
                result.history->Merge(history);
            current=merged;
            return true;
        }
        return false;
    };
    // The periodic continuation repair targets the analytic single-edge
    // prism/revolution construction.  A PipeShell over a mixed spline route
    // already owns its exact connected lateral surface; reshaping periodic
    // fragments there can invalidate an otherwise valid Boolean result.
    if(!has_general_route)
        while(merge_periodic_continuation()) {}

    // The two straight sides of the exact arc-and-two-lines profile lie on
    // the original support planes. OCCT can retain their end sections as
    // coplanar Boolean seams. Remove only those redundant new seams: every
    // surviving input edge and every non-coplanar descendant is protected so
    // the cleanup cannot rebuild unrelated Body topology.
    ShapeUpgrade_UnifySameDomain cleanup(current,true,true,false);
    cleanup.SetLinearTolerance(tolerance);
    cleanup.SetAngularTolerance(1.0e-9);
    TopTools_IndexedMapOfShape current_edges;
    TopExp::MapShapes(current,TopAbs_EDGE,current_edges);
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
    // Preserve every current boundary that is not the new coplanar seam we
    // are deliberately removing. Following only the Boolean history from
    // the input is insufficient for disconnected split descendants: OCCT may
    // publish an unchanged sibling directly in the result without listing it
    // in Modified/Generated.
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
            "ZIMA Fillet could not remove auxiliary result seams");
    const bool cleanup_valid=BRepCheck_Analyzer(cleanup.Shape(),true,true,
        false).IsValid();
    if(cleanup_valid) {
        if(const auto cleanup_history=cleanup.History();
                !cleanup_history.IsNull())
            result.history->Merge(cleanup_history);
        current=cleanup.Shape();
    } else if(!has_general_route) {
        throw std::runtime_error(
            "Planar edge Fillet same-domain cleanup produced an invalid Body");
    }
    result.shape=current;
    TopTools_IndexedMapOfShape result_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,result_faces);
    result.fillet_faces.resize(edges.size());
    for(std::size_t index=0;index<cutters.size();++index) {
        std::vector<TopoDS_Face> cutter_faces;
        const auto append=[&](const TopoDS_Shape& shape) {
            if(shape.ShapeType()!=TopAbs_FACE||
               !result_faces.Contains(shape))return;
            const auto face=TopoDS::Face(shape);
            if(std::ranges::none_of(cutter_faces,
                    [&](const auto& current_face) {
                        return current_face.IsSame(face);
                    }))
                cutter_faces.push_back(face);
        };
        for(const auto& circular_face:cutters[index].circular_faces) {
            append(circular_face);
            for(const auto* descendants:{
                    &result.history->Modified(circular_face),
                    &result.history->Generated(circular_face)})
                for(const auto& descendant:*descendants)append(descendant);
        }
        result.metrics.generated_fillet_faces+=
            cutter_faces.size();
        if(cutter_faces.empty())
            throw std::runtime_error(
                "Planar edge Fillet lost its circular result face ancestry");
        for(const auto source_job:cutters[index].source_jobs)
            result.fillet_faces[source_job]=cutter_faces;
    }
    for(const auto& connector:node_connectors) {
        if(!connector.sphere_center)continue;
        std::vector<TopoDS_Face> spherical_faces;
        for(int index=1;index<=result_faces.Extent();++index) {
            const auto face=TopoDS::Face(result_faces.FindKey(index));
            const BRepAdaptor_Surface surface(face,true);
            if(surface.GetType()!=GeomAbs_Sphere)continue;
            const auto sphere=surface.Sphere();
            if(sphere.Location().Distance(*connector.sphere_center)>
                    tolerance||std::abs(sphere.Radius()-radius)>tolerance)
                continue;
            spherical_faces.push_back(face);
        }
        if(spherical_faces.empty())
            throw std::runtime_error(
                "ZIMA Fillet lost its spherical node face");
        result.metrics.generated_fillet_faces+=spherical_faces.size();
        for(const auto job:connector.jobs)
            for(const auto& face:spherical_faces)
                if(std::ranges::none_of(result.fillet_faces[job],
                        [&](const auto& current_face) {
                            return current_face.IsSame(face);
                        }))
                    result.fillet_faces[job].push_back(face);
    }
    result.metrics.boolean_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-boolean_started).count();
    result.source_edges.assign(edges.begin(),edges.end());
    result.generated_from_edges.resize(edges.size());
    for(std::size_t index=0;index<edges.size();++index) {
        auto& generated=result.generated_from_edges[index];
        for(const auto& shape:result.history->Generated(edges[index]))
            generated.Append(shape);
        for(const auto& face:result.fillet_faces[index])
            if(std::ranges::none_of(generated,[&](const auto& shape) {
                    return shape.IsSame(face);
                }))generated.Append(face);
    }
    const auto validation_started=Clock::now();
    if(!BRepCheck_Analyzer(result.shape,true,true,false).IsValid())
        throw std::runtime_error(
            "Planar edge Fillet produced an invalid Body");
    result.metrics.validation_ms=
        std::chrono::duration<double,std::milli>(
            Clock::now()-validation_started).count();
    return result;
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeFilletResult::Modified(
        const TopoDS_Shape& source) const {
    return history->Modified(source);
}

const NCollection_List<TopoDS_Shape>& OcctPlanarEdgeFilletResult::Generated(
        const TopoDS_Shape& source) const {
    for(std::size_t index=0;index<source_edges.size();++index)
        if(source_edges[index].IsSame(source))
            return generated_from_edges[index];
    return history->Generated(source);
}

bool OcctPlanarEdgeFilletResult::IsDeleted(
        const TopoDS_Shape& source) const {
    return history->IsRemoved(source);
}

} // namespace zima::kernel
