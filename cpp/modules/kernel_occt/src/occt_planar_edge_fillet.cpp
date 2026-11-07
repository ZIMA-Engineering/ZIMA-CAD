#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
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
#include <GC_MakeArcOfCircle.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
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
    const auto first=curve.Value(curve.FirstParameter());
    const auto last=curve.Value(curve.LastParameter());
    const auto point=BRep_Tool::Pnt(vertex);
    const auto opposite=point.Distance(first)<=point.Distance(last)
        ?last:first;
    const gp_Vec direction(point,opposite);
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

NodeConnector make_step_fillet_connector(const TopoDS_Vertex& vertex,
        gp_Dir first_branch,gp_Dir second_branch,gp_Dir third_branch,
        double radius,double tolerance,std::vector<std::size_t> jobs,
        bool adds_material) {
    for(const auto pair:std::array{
            std::pair{first_branch,second_branch},
            std::pair{first_branch,third_branch},
            std::pair{second_branch,third_branch}})
        if(std::abs(pair.first.Dot(pair.second))>1.0e-7)
            throw std::invalid_argument(
                "ZIMA Fillet requires an orthogonal step node");
    first_branch.Reverse();
    second_branch.Reverse();
    std::array axes{first_branch,second_branch,third_branch};
    if(gp_Vec(axes[0]).Crossed(gp_Vec(axes[1])).Dot(
            gp_Vec(axes[2]))<0.)
        std::swap(axes[1],axes[2]);
    const auto origin=BRep_Tool::Pnt(vertex);
    BRepPrimAPI_MakeBox corner(gp_Ax2(origin,axes[2],axes[0]),
        radius,radius,radius);
    const auto cylinder=[&](const gp_Dir& axis,const gp_Dir& first_offset,
            const gp_Dir& second_offset) {
        const auto center=origin.Translated(radius*(
            gp_Vec(first_offset)+gp_Vec(second_offset)));
        return BRepPrimAPI_MakeCylinder(gp_Ax2(center,axis),radius,
            radius).Shape();
    };
    const auto first_cylinder=cylinder(first_branch,second_branch,
        third_branch);
    const auto second_cylinder=cylinder(second_branch,first_branch,
        third_branch);
    BRepAlgoAPI_Cut first_cut(corner.Shape(),first_cylinder);
    first_cut.SetNonDestructive(true);first_cut.SetRunParallel(false);
    first_cut.SetFuzzyValue(tolerance);first_cut.Build();
    if(!first_cut.IsDone()||first_cut.Shape().IsNull())
        throw std::runtime_error(
            "ZIMA Fillet could not construct its step-node domain");
    BRepAlgoAPI_Cut second_cut(first_cut.Shape(),second_cylinder);
    second_cut.SetNonDestructive(true);second_cut.SetRunParallel(false);
    second_cut.SetFuzzyValue(tolerance);second_cut.Build();
    if(!second_cut.IsDone()||second_cut.Shape().IsNull()||
            !BRepCheck_Analyzer(second_cut.Shape()).IsValid())
        throw std::runtime_error(
            "ZIMA Fillet could not construct a valid step-node connector");
    return {second_cut.Shape(),std::nullopt,std::move(jobs),
        adds_material};
}

int fillet_material_octants(const TopoDS_Solid& material,
        const gp_Pnt& origin,const std::array<gp_Dir,3>& directions,
        double probe,double tolerance) {
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
                    "ZIMA Fillet could not classify a step node");
        }
    return inside;
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

void clip_fillet_tool(TopoDS_Shape& tool,
        std::vector<TopoDS_Face>& treatment_faces,
        const TopoDS_Face& face,const gp_Pnt& inside,double tolerance) {
    if(face.IsNull())return;
    const BRepAdaptor_Surface end_support(face,true);
    BRepBuilderAPI_MakeFace end_plane(end_support.Plane());
    if(!end_plane.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct an end plane");
    BRepPrimAPI_MakeHalfSpace half_space(end_plane.Face(),inside);
    if(!half_space.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct an end half-space");
    BRepAlgoAPI_Common common(tool,half_space.Solid());
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
                        return BRep_Tool::Surface(source)==
                            BRep_Tool::Surface(candidate);
                    }))append(candidate);
        }
    if(clipped_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet trim lost its circular face");
    treatment_faces=std::move(clipped_faces);
    tool=common.Shape();
}

TopoDS_Face automatic_angled_end_face(const TopoDS_Solid& material,
        const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& tangent,double tolerance) {
    struct Candidate {
        TopoDS_Face face;
        gp_Pln plane;
        int support_adjacency{};
    };
    const auto meets_support_at_endpoint=[&](const TopoDS_Face& candidate,
            const TopoDS_Face& support) {
        TopTools_IndexedMapOfShape support_edges;
        TopExp::MapShapes(support,TopAbs_EDGE,support_edges);
        for(TopExp_Explorer item(candidate,TopAbs_EDGE);item.More();
                item.Next()) {
            const auto edge=TopoDS::Edge(item.Current());
            if(!support_edges.Contains(edge))continue;
            TopTools_IndexedMapOfShape vertices;
            TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
            for(int index=1;index<=vertices.Extent();++index)
                if(BRep_Tool::Pnt(TopoDS::Vertex(
                        vertices.FindKey(index))).Distance(endpoint)<=
                        tolerance)return true;
        }
        return false;
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
        // A perpendicular terminal face already agrees with the ordinary
        // exact endpoint.  Only an oblique face needs an extended tool and a
        // subsequent clip; a parallel face cannot terminate this route.
        if(incidence<=1.0e-8||incidence>=1.-1.0e-8)continue;
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            continue;
        const auto plane=support.Plane();
        const int adjacency=
            (meets_support_at_endpoint(face,first_support)?1:0)+
            (meets_support_at_endpoint(face,second_support)?1:0);
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
    const auto equally_strong=std::ranges::count_if(candidates,
        [&](const auto& candidate) {
            return candidate.support_adjacency==strongest;
        });
    // At a Boolean corner several planes can contain the terminal point.  A
    // real cap continues both support faces into that point and is therefore
    // the only safe automatic choice.  When no candidate has that topology,
    // retain the old conservative rule: accept only one unambiguous plane.
    if((strongest==2||candidates.size()==1)&&equally_strong==1)
        return std::ranges::find_if(candidates,[&](const auto& candidate) {
            return candidate.support_adjacency==strongest;
        })->face;
    return {};
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

    auto effective_end_faces=end_faces;
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(effective_end_faces.first.IsNull())
            effective_end_faces.first=automatic_angled_end_face(material,
                first_face,second_face,path.first,path.first_tangent,tolerance);
        if(effective_end_faces.last.IsNull())
            effective_end_faces.last=automatic_angled_end_face(material,
                first_face,second_face,path.last,path.last_tangent,tolerance);
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
    const double first_extension=extension(effective_end_faces.first,
        path.first,path.first_tangent);
    const double last_extension=extension(effective_end_faces.last,
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
    const auto tangent_to_end=[](const TopoDS_Face& face,
            const gp_Dir& tangent) {
        return !face.IsNull()&&
            std::abs(BRepAdaptor_Surface(face,true).Plane().Axis().
                Direction().Dot(tangent))<=1.0e-8;
    };
    if(!tangent_to_end(effective_end_faces.first,path.first_tangent))
        clip_fillet_tool(tool,circular_faces,effective_end_faces.first,
            path.middle,tolerance);
    if(!tangent_to_end(effective_end_faces.last,path.last_tangent))
        clip_fillet_tool(tool,circular_faces,effective_end_faces.last,
            path.middle,tolerance);
    return {tool,std::move(circular_faces),
        analytic(source,std::max(first_extension,last_extension)),true,
        midpoint.profile.wire,midpoint.profile.circular_edge,{}};
}

Cutter make_cutter(const TopoDS_Edge& edge,
        const TopoDS_Face& first_face,
        const TopoDS_Face& second_face,double radius,double tolerance,
        const TopoDS_Solid& material,
        const OcctPlanarEdgeEndFaces& end_faces,
        bool profile_only=false,
        OcctPlanarEdgeFilletEndMode end_mode=
            OcctPlanarEdgeFilletEndMode::Perpendicular) {
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

    const double tangent_distance=radius/
        std::tan(interior_angle/2.);
    const double center_distance=radius/
        std::sin(interior_angle/2.);
    if(!std::isfinite(tangent_distance)||
       !std::isfinite(center_distance))
        throw std::invalid_argument(
            "Planar edge Fillet radius is invalid");
    auto effective_end_faces=end_faces;
    const bool automatic_request=!profile_only&&removes_material&&
        path.kind==EdgeFilletPathKind::Linear&&end_mode==
            OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope;
    if(!profile_only&&end_mode==
            OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(effective_end_faces.first.IsNull())
            effective_end_faces.first=automatic_angled_end_face(
                material,first_face,second_face,path.first,
                path.first_tangent,tolerance);
        if(effective_end_faces.last.IsNull())
            effective_end_faces.last=automatic_angled_end_face(
                material,first_face,second_face,path.last,
                path.last_tangent,tolerance);
    }
    const bool envelope_first=automatic_request&&
        effective_end_faces.first.IsNull()&&
        !has_perpendicular_terminal_face(material,first_face,second_face,
            path.first,path.first_tangent,tolerance);
    const bool envelope_last=automatic_request&&
        effective_end_faces.last.IsNull()&&
        !has_perpendicular_terminal_face(material,first_face,second_face,
            path.last,path.last_tangent,tolerance);
    const auto tangent_to_end_face=[](const TopoDS_Face& face,
            const gp_Dir& tangent) {
        if(face.IsNull())return false;
        const BRepAdaptor_Surface support(face,true);
        return support.GetType()==GeomAbs_Plane&&std::abs(
            support.Plane().Axis().Direction().Dot(tangent))<=1.0e-8;
    };
    const bool tangent_first=tangent_to_end_face(
        effective_end_faces.first,path.first_tangent);
    const bool tangent_last=tangent_to_end_face(
        effective_end_faces.last,path.last_tangent);
    const auto axial_extension=[&](const TopoDS_Face& face,
            const gp_Pnt& endpoint,const gp_Dir& tangent,
            double cross_span) {
        if(face.IsNull())return 0.;
        const BRepAdaptor_Surface support(face,true);
        if(support.GetType()!=GeomAbs_Plane)
            throw std::invalid_argument(
                "Planar edge Fillet end face must be planar");
        if(support.Plane().Distance(endpoint)>tolerance)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            throw std::invalid_argument(
                "Planar edge Fillet end face does not contain the route endpoint");
        const auto normal=support.Plane().Axis().Direction();
        const double incidence=std::abs(normal.Dot(tangent));
        if(incidence<=1.0e-8)return 0.;
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
    double first_extension=effective_end_faces.first.IsNull()&&
            envelope_first
        ?envelope_extension
        :axial_extension(effective_end_faces.first,path.first,
            path.first_tangent,cross_span);
    double last_extension=effective_end_faces.last.IsNull()&&
            envelope_last
        ?envelope_extension
        :axial_extension(effective_end_faces.last,path.last,
            path.last_tangent,cross_span);
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
    const double extension=std::max(first_extension,last_extension);
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
    if(!tangent_first)
        clip_fillet_tool(tool,circular_faces,effective_end_faces.first,
            interior,tolerance);
    if(!tangent_last)
        clip_fillet_tool(tool,circular_faces,effective_end_faces.last,
            interior,tolerance);
    return {tool,std::move(circular_faces),analytic_domain(extension),
        adds_material,midpoint_profile.wire,
        midpoint_profile.circular_edge,{}};
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

    // Construct the exact profile on an analytic route member and let the
    // pipe carry it over the remaining connected curve.  A circle is the
    // strongest anchor because it establishes radial transport; otherwise a
    // line avoids imposing a false circle model on a following ellipse or
    // another OCCT curve produced by an earlier feature intersection.
    std::optional<std::size_t> circular_anchor,line_anchor;
    for(const auto index:route) {
        const auto kind=BRepAdaptor_Curve(jobs[index].edge).GetType();
        if(kind==GeomAbs_Circle&&!circular_anchor)circular_anchor=index;
        if(kind==GeomAbs_Line&&!line_anchor)line_anchor=index;
    }
    const auto anchor=circular_anchor.value_or(line_anchor.value_or(
        route[route.size()/2]));
    auto profile=make_cutter(jobs[anchor].edge,jobs[anchor].first_face,
        jobs[anchor].second_face,radius,tolerance,jobs[anchor].material,
        {},true);

    const auto start_vertex=endpoints(ordered.front())[0];
    const auto end_vertex=endpoints(ordered.back())[1];
    const gp_Pnt start_point=BRep_Tool::Pnt(start_vertex);
    const gp_Pnt end_point=BRep_Tool::Pnt(end_vertex);
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
    TopoDS_Face start_face,end_face;
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
        auto& destination=first?start_face:end_face;
        if(!destination.IsNull()&&!destination.IsSame(face))
            throw std::invalid_argument(
                "Planar edge Fillet route endpoint has conflicting end faces");
        destination=face;
    };
    if(!end_faces.empty())
        for(const auto index:route) {
            assign_end_face(end_faces[index].first);
            assign_end_face(end_faces[index].last);
        }
    const auto& start_job=jobs[route.front()];
    const auto& end_job=jobs[route.back()];
    const bool automatic_request=!profile.adds_material&&end_mode==
        OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope;
    if(end_mode==OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope) {
        if(start_face.IsNull())
            start_face=automatic_angled_end_face(start_job.material,
                start_job.first_face,start_job.second_face,start_point,
                start_outward,tolerance);
        if(end_face.IsNull())
            end_face=automatic_angled_end_face(end_job.material,
                end_job.first_face,end_job.second_face,end_point,
                end_outward,tolerance);
    }
    const bool envelope_first=automatic_request&&start_face.IsNull()&&
        !has_perpendicular_terminal_face(start_job.material,
            start_job.first_face,start_job.second_face,start_point,
            start_outward,tolerance);
    const bool envelope_last=automatic_request&&end_face.IsNull()&&
        !has_perpendicular_terminal_face(end_job.material,
            end_job.first_face,end_job.second_face,end_point,end_outward,
            tolerance);
    const auto tangent_to_end_face=[](const TopoDS_Face& face,
            const gp_Dir& tangent) {
        if(face.IsNull())return false;
        const BRepAdaptor_Surface support(face,true);
        return support.GetType()==GeomAbs_Plane&&std::abs(
            support.Plane().Axis().Direction().Dot(tangent))<=1.0e-8;
    };
    const bool tangent_first=tangent_to_end_face(start_face,start_outward);
    const bool tangent_last=tangent_to_end_face(end_face,end_outward);
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
    const auto extension=[&](const TopoDS_Face& face,
            const gp_Dir& outward,const TopoDS_Solid& material,
            bool envelope) {
        if(face.IsNull())return envelope
            ?body_envelope_extension(material,cross_span,tolerance):0.;
        const BRepAdaptor_Surface support(face,true);
        const double incidence=std::abs(
            support.Plane().Axis().Direction().Dot(outward));
        // A route can finish tangent to a selected Up To face.  Its exact
        // terminal vertex then already supplies the longitudinal limit; the
        // supporting plane cannot provide a second line/plane intersection
        // and must not cause an artificial axial extension.  A longitudinal
        // half-space clip would cut the profile along the complete route, so
        // the tangent-face flags below intentionally skip that clip as well.
        if(incidence<=1.0e-8)return 0.;
        return cross_span/incidence+tolerance;
    };
    const double first_extension=extension(start_face,start_outward,
        start_job.material,envelope_first);
    const double last_extension=extension(end_face,end_outward,
        end_job.material,envelope_last);

    BRepBuilderAPI_MakeWire spine;
    if(first_extension>0.) {
        const auto extended=BRepBuilderAPI_MakeVertex(
            start_point.Translated(gp_Vec(start_outward)*first_extension)).
                Vertex();
        spine.Add(BRepBuilderAPI_MakeEdge(extended,start_vertex).Edge());
    }
    for(const auto& edge:ordered)spine.Add(edge);
    if(last_extension>0.) {
        const auto extended=BRepBuilderAPI_MakeVertex(
            end_point.Translated(gp_Vec(end_outward)*last_extension)).
                Vertex();
        spine.Add(BRepBuilderAPI_MakeEdge(end_vertex,extended).Edge());
    }
    if(!spine.IsDone())
        throw std::runtime_error(
            "Planar edge Fillet could not construct one route wire");

    BRepOffsetAPI_MakePipeShell sweep(spine.Wire());
    sweep.SetMode(false);
    sweep.SetTolerance(tolerance,tolerance,1.0e-6);
    sweep.Add(profile.midpoint_profile,false,false);
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
    if(circular_faces.empty())
        throw std::runtime_error(
            "Planar edge Fillet route sweep omitted its circular face");
    profile.solid=sweep.Shape();
    profile.circular_faces=std::move(circular_faces);
    const auto anchor_path=edge_path(jobs[anchor].edge,tolerance);
    if(!tangent_first)
        clip_fillet_tool(profile.solid,profile.circular_faces,start_face,
            anchor_path.middle,tolerance);
    if(!tangent_last)
        clip_fillet_tool(profile.solid,profile.circular_faces,end_face,
            anchor_path.middle,tolerance);
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
        const auto path=edge_path(job.edge,tolerance);
        gp_Pnt point;gp_Vec derivative;
        curve.D1(path.middle_parameter,point,derivative);
        if(derivative.SquareMagnitude()<=1.0e-18)return false;
        const gp_Dir tangent(derivative);
        const auto first=inward_direction(job.edge,job.first_face,curve,
            path.middle_parameter,tangent,tolerance);
        const auto second=inward_direction(job.edge,job.second_face,curve,
            path.middle_parameter,tangent,tolerance);
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
            false,end_mode);
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
    std::vector<SharedEndpoint> selected_nodes;
    for(std::size_t job=0;job<jobs.size();++job) {
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
            std::vector<TopoDS_Edge> unselected;
            for(TopExp_Explorer item(material,TopAbs_EDGE);
                    item.More();item.Next()) {
                const auto edge=TopoDS::Edge(item.Current());
                TopTools_IndexedMapOfShape vertices;
                TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
                if(!vertices.Contains(node.vertex)||
                        std::ranges::any_of(node.jobs,
                            [&](const auto job) {
                                return jobs[job].edge.IsSame(edge);
                            }))continue;
                if(std::ranges::none_of(unselected,
                        [&](const auto& current) {
                            return current.IsSame(edge);
                        }))unselected.push_back(edge);
            }
            if(unselected.size()==1) {
                const auto third=node_outgoing_tangent(unselected.front(),
                    node.vertex,tolerance);
                const std::array branches{directions[0],directions[1],third};
                const int occupied=fillet_material_octants(material,
                    BRep_Tool::Pnt(node.vertex),branches,
                    std::max(100.*tolerance,radius*.05),tolerance);
                if((adds_material&&occupied==5)||
                        (!adds_material&&occupied==3)) {
                    node_connectors.push_back(make_step_fillet_connector(
                        node.vertex,directions[0],directions[1],third,
                        radius,tolerance,node.jobs,adds_material));
                }
            }
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
    // Three rolling bands leave the trihedral node corner outside their
    // pairwise cylindrical domains. Its exact connector is the local
    // radius-sized corner box minus the sphere centred one radius along all
    // three outgoing branches. A two-branch step uses the same local box
    // minus both perpendicular rolling cylinders. Applying either connector
    // after the bands works identically as a signed CUT for an outer node and
    // a signed FUSE for an inner node.
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
    // The two straight sides of the exact arc-and-two-lines profile lie on
    // the original support planes. OCCT can retain their end sections as
    // coplanar Boolean seams. Remove only those redundant new seams: every
    // surviving input edge and every non-coplanar descendant is protected so
    // the cleanup cannot rebuild unrelated Body topology.
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
    // Preserve every current boundary that is not the new coplanar seam we
    // are deliberately removing.  Following only the Boolean history from
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
    if(const auto cleanup_history=cleanup.History();
            !cleanup_history.IsNull())
        result.history->Merge(cleanup_history);
    current=cleanup.Shape();
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
