#include "ordered_up_to.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace zima::kernel::detail {
namespace {
[[noreturn]] void fail(OrderedLimitFailure reason,std::size_t row,std::size_t other) {
    throw OrderedLimitError(reason,row,other);
}
double area(const TopoDS_Shape& shape) {
    GProp_GProps props;BRepGProp::SurfaceProperties(shape,props);return props.Mass();
}
TopoDS_Face only_face(const TopoDS_Shape& shape,std::size_t row,std::size_t other) {
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
    if(faces.Extent()!=1)fail(OrderedLimitFailure::EmptyPatch,row,other);
    return TopoDS::Face(faces(1));
}

TopoDS_Face extend_support(const TopoDS_Face& authored,const Bnd_Box& box,
        double tolerance,std::size_t row) {
    // Plane bounds come from the actual operation envelope, not a global
    // million-unit rectangle. Other bounded surfaces retain their exact basis.
    auto surface=BRep_Tool::Surface(authored);
    BRepAdaptor_Surface adaptor(authored,false);
    bool planar=adaptor.GetType()==GeomAbs_Plane;
    if(!planar) {
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        const double scale=std::max({1.,std::abs(x0),std::abs(x1),std::abs(y0),std::abs(y1),std::abs(z0),std::abs(z1)});
        // Filling may represent an exactly planar authored boundary as a
        // bounded B-spline. Recognize only roundoff-level planarity, not a
        // tessellated approximation or a model-tolerance flattening.
        const double roundoff=std::min(tolerance*1e-3,128*std::numeric_limits<double>::epsilon()*scale);
        const auto spline=occ::down_cast<Geom_BSplineSurface>(surface);
        const auto bezier=occ::down_cast<Geom_BezierSurface>(surface);
        GeomLib_IsPlanarSurface check(surface,spline||bezier?tolerance:roundoff);
        bool certified=check.IsPlanar();
        // Obtain a candidate plane at kernel tolerance, then certify every
        // pole independently at roundoff tolerance. The PCA classifier itself
        // is not reliable at that tighter threshold. Positive-weight bases
        // keep the entire authored domain inside the poles' planar bound.
        if(certified&&spline)for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v)
            certified=certified&&check.Plan().Distance(spline->Pole(u,v))<=roundoff;
        if(certified&&bezier)for(int u=1;u<=bezier->NbUPoles();++u)for(int v=1;v<=bezier->NbVPoles();++v)
            certified=certified&&check.Plan().Distance(bezier->Pole(u,v))<=roundoff;
        if(certified) {
            auto plane=check.Plan();double u0,u1,v0,v1;BRepTools::UVBounds(authored,u0,u1,v0,v1);
            GeomLProp_SLProps props(surface,(u0+u1)*.5,(v0+v1)*.5,1,tolerance);
            if(!props.IsNormalDefined())fail(OrderedLimitFailure::InvalidInput,row,row);
            if(plane.Axis().Direction().Dot(props.Normal())<0)plane.SetAxis(plane.Axis().Reversed());
            surface=new Geom_Plane(plane);planar=true;
        }
    }
    const auto type=adaptor.GetType();
    // Analytic supports have a defined continuation. A general free surface
    // retains its real trimmed domain, including holes, rather than its UV box.
    if(!planar&&type!=GeomAbs_Cylinder&&type!=GeomAbs_Cone&&type!=GeomAbs_Sphere&&type!=GeomAbs_Torus)return authored;
    double u0{},u1{},v0{},v1{};surface->Bounds(u0,u1,v0,v1);
    if(planar||adaptor.GetType()==GeomAbs_Cylinder||
            adaptor.GetType()==GeomAbs_Cone) {
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        double pu0=std::numeric_limits<double>::infinity(),pv0=pu0,pu1=-pu0,pv1=-pu0;
        for(unsigned corner=0;corner<8;++corner) {
            GeomAPI_ProjectPointOnSurf projected(gp_Pnt(corner&1?x1:x0,
                corner&2?y1:y0,corner&4?z1:z0),surface);
            if(projected.NbPoints()==0)fail(OrderedLimitFailure::InvalidInput,row,row);
            // Include every exact projection. A periodic seam must not select a
            // nearer screen-space branch or truncate the opposite side.
            for(int i=1;i<=projected.NbPoints();++i) {
                double u,v;projected.Parameters(i,u,v);
                pu0=std::min(pu0,u);pu1=std::max(pu1,u);
                pv0=std::min(pv0,v);pv1=std::max(pv1,v);
            }
        }
        if(!surface->IsUPeriodic()){u0=pu0;u1=pu1;}
        if(!surface->IsVPeriodic()){v0=pv0;v1=pv1;}
    }
    if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||
            Precision::IsInfinite(u0)||Precision::IsInfinite(u1)||
            Precision::IsInfinite(v0)||Precision::IsInfinite(v1)||
            u1-u0<=Precision::PConfusion()||v1-v0<=Precision::PConfusion())
        fail(OrderedLimitFailure::InvalidInput,row,row);
    BRepBuilderAPI_MakeFace made(surface,u0,u1,v0,v1,tolerance);
    if(!made.IsDone())fail(OrderedLimitFailure::InvalidInput,row,row);
    auto result=made.Face();result.Orientation(authored.Orientation());return result;
}

TopoDS_Solid positive_half(const TopoDS_Face& support,const Bnd_Box& all_supports,double tolerance,std::size_t row) {
    double u0,u1,v0,v1;BRepTools::UVBounds(support,u0,u1,v0,v1);
    const auto surface=BRep_Tool::Surface(support);
    auto half_support=support;
    BRepAdaptor_Surface basis(support,false);
    if(basis.GetType()==GeomAbs_Plane) {
        // UV rectangles bound intersection work, not material-side selection.
        // A rotated rectangle can protrude beyond the original world envelope.
        // Bound the half-space face from all actual extended supports so its
        // artificial rim cannot clip any neighbouring patch.
        half_support=extend_support(support,all_supports,tolerance,row);
    }
    // Avoid periodic seams and singularities; this point only orients the
    // runtime half-space. It is never a persistent reference identity.
    for(double t:{.37,.61,.23}) {
        const double u=u0+(u1-u0)*t,v=v0+(v1-v0)*t;
        GeomLProp_SLProps props(surface,u,v,2,tolerance);
        if(!props.IsNormalDefined())continue;
        auto normal=props.Normal();if(support.Orientation()==TopAbs_REVERSED)normal.Reverse();
        const auto seed=surface->Value(u,v);
        double step=1.;
        if(props.IsCurvatureDefined()) {
            const double curvature=std::max(std::abs(props.MaxCurvature()),std::abs(props.MinCurvature()));
            if(curvature>0)step=std::min(step,.01/curvature);
        }
        // A fixed one-millimetre step can cross a small reversed sphere or
        // cylinder and label its opposite material side as positive. Stay in
        // the local normal neighbourhood and prove the closest support point.
        for(unsigned attempt=0;attempt<8&&step>=10*tolerance;++attempt,step*=.1) {
            const auto point=seed.Translated(gp_Vec(normal)*step);
            GeomAPI_ProjectPointOnSurf nearest(point,surface);
            if(nearest.NbPoints()==0||nearest.NearestPoint().Distance(seed)>std::max(tolerance,step*1e-6))continue;
            return BRepPrimAPI_MakeHalfSpace(half_support,point).Solid();
        }
    }
    fail(OrderedLimitFailure::InvalidInput,row,row);
}

TopoDS_Shape positive_part(const TopoDS_Shape& shape,const TopoDS_Solid& half,
        double tolerance,std::size_t row,std::size_t other) {
    BRepAlgoAPI_Common common;TopTools_ListOfShape args,tools;args.Append(shape);tools.Append(half);
    common.SetArguments(args);common.SetTools(tools);common.SetNonDestructive(true);
    common.SetFuzzyValue(tolerance);common.Build();
    if(!common.IsDone()||common.Shape().IsNull())fail(OrderedLimitFailure::EmptyPatch,row,other);
    return common.Shape();
}

TopoDS_Face retain(const TopoDS_Face& patch,const TopoDS_Face& authored,
        const TopoDS_Solid& positive,std::optional<bool>& choice,
        double tolerance,std::size_t row,std::size_t other) {
    if(!choice) {
        const double total=area(authored),inside=area(positive_part(authored,positive,tolerance,row,other));
        const double threshold=std::max(tolerance*tolerance*100,total*1e-9);
        if(total<=threshold||(inside>threshold&&total-inside>threshold))
            fail(OrderedLimitFailure::AmbiguousRetainedPatch,row,other);
        choice=inside>threshold;
    }
    TopoDS_Shape retained;
    if(*choice)retained=positive_part(patch,positive,tolerance,row,other);
    else {
        BRepAlgoAPI_Cut cut;TopTools_ListOfShape args,tools;args.Append(patch);tools.Append(positive);
        cut.SetArguments(args);cut.SetTools(tools);cut.SetNonDestructive(true);
        cut.SetFuzzyValue(tolerance);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())fail(OrderedLimitFailure::EmptyPatch,row,other);
        retained=cut.Shape();
    }
    if(area(retained)<=tolerance*tolerance*100)fail(OrderedLimitFailure::EmptyPatch,row,other);
    TopTools_IndexedMapOfShape pieces;TopExp::MapShapes(retained,TopAbs_FACE,pieces);
    if(pieces.Extent()>1) {
        // A cut can split one periodic support at its parameter seam. Merge
        // connected exact same-domain descendants of this one source only.
        ShapeUpgrade_UnifySameDomain unify(retained,false,true,false);
        unify.SetSafeInputMode(true);unify.SetLinearTolerance(tolerance);unify.Build();retained=unify.Shape();
    }
    return only_face(retained,row,other);
}

struct Adjacency {bool tangent{};std::optional<gp_Lin> line;};
Adjacency check_adjacency(const TopoDS_Face& a,const TopoDS_Face& b,
        double tolerance,std::size_t row,std::size_t other) {
    BRepAlgoAPI_Section section(a,b,false);section.SetFuzzyValue(tolerance);
    section.Approximation(false);section.Build();
    if(!section.IsDone())fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
    occ::handle<TopTools_HSequenceOfShape> edges=new TopTools_HSequenceOfShape;
    for(TopExp_Explorer edge(section.Shape(),TopAbs_EDGE);edge.More();edge.Next())edges->Append(edge.Current());
    if(edges->IsEmpty())fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
    occ::handle<TopTools_HSequenceOfShape> connected=new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,tolerance,false,connected);
    if(connected->Length()!=1)fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
    // Tangency does not split either support into positive/negative patches.
    // Its authored seam must be verified separately instead of inventing a side.
    TopExp_Explorer edge(section.Shape(),TopAbs_EDGE);BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
    const auto point=curve.Value((curve.FirstParameter()+curve.LastParameter())*.5);
    gp_Vec normals[2];unsigned index=0;
    for(const auto& face:{a,b}) {
        const auto surface=BRep_Tool::Surface(face);GeomAPI_ProjectPointOnSurf projected(point,surface);
        if(projected.NbPoints()==0)fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
        double u,v;projected.LowerDistanceParameters(u,v);GeomLProp_SLProps props(surface,u,v,1,tolerance);
        if(!props.IsNormalDefined())fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
        normals[index++]=gp_Vec(props.Normal());
    }
    const bool tangent=normals[0].Crossed(normals[1]).Magnitude()<1e-8;
    return {tangent,tangent&&curve.GetType()==GeomAbs_Line?std::optional<gp_Lin>{curve.Line()}:std::nullopt};
}

bool extend_analytic_tangent_line(TopoDS_Face& a,TopoDS_Face& b,const TopoDS_Face& authored_a,
        const TopoDS_Face& authored_b,const gp_Lin& seam,const Bnd_Box& envelope,
        double tolerance,std::size_t row,std::size_t other) {
    const std::array<TopoDS_Face,2> original{{a,b}};std::array<std::optional<gp_Cylinder>,2> cylinders;
    std::array<gp_Vec,2> normals;bool curved=false;
    for(unsigned side=0;side<2;++side) {
        const BRepAdaptor_Surface surface(original[side],false);
        if(surface.GetType()==GeomAbs_Plane) {
            const auto plane=surface.Plane();
            if(plane.Distance(seam.Location())>tolerance||std::abs(plane.Axis().Direction().Dot(seam.Direction()))>1e-10)return false;
            normals[side]=gp_Vec(plane.Axis().Direction());
        } else if(surface.GetType()==GeomAbs_Cylinder) {
            const auto cylinder=surface.Cylinder();
            if(!cylinder.Axis().Direction().IsParallel(seam.Direction(),1e-10))return false;
            const gp_Vec delta(cylinder.Location(),seam.Location()),axis(cylinder.Axis().Direction());
            const auto radial=delta-axis*delta.Dot(axis);
            if(std::abs(radial.Magnitude()-cylinder.Radius())>tolerance)return false;
            normals[side]=radial.Normalized();cylinders[side]=cylinder;curved=true;
        } else return false;
    }
    if(!curved||normals[0].Crossed(normals[1]).Magnitude()>1e-8)return false;
    // Parallel analytic generators certify the complete tangent seam. A
    // transverse curtain extends the selected authored branch along that seam;
    // the tangent radial hemisphere prevents continuation around the opposite
    // side of a periodic cylinder. Neither derived choice is a normal-side
    // choice against the adjacent tangent support, so neither is persisted as one.
    const auto curtain=extend_support(BRepBuilderAPI_MakeFace(gp_Pln(seam.Location(),
        gp_Dir(normals[0].Crossed(gp_Vec(seam.Direction())))),-1.,1.,-1.,1.).Face(),envelope,tolerance,row);
    const auto positive=positive_half(curtain,envelope,tolerance,row);
    const std::array<TopoDS_Face,2> authored{{authored_a,authored_b}};std::array<TopoDS_Face,2> result;
    for(unsigned side=0;side<2;++side) {
        std::optional<bool> selected;
        result[side]=retain(original[side],authored[side],positive,selected,tolerance,side?other:row,side?row:other);
        if(cylinders[side]) {
            const auto branch=extend_support(BRepBuilderAPI_MakeFace(gp_Pln(cylinders[side]->Location(),
                gp_Dir(normals[side])),-1.,1.,-1.,1.).Face(),envelope,tolerance,side?other:row);
            const auto hemisphere=positive_half(branch,envelope,tolerance,side?other:row);
            std::optional<bool> near_seam=true;
            result[side]=retain(result[side],authored[side],hemisphere,near_seam,tolerance,side?other:row,side?row:other);
        }
    }
    a=result[0];b=result[1];return true;
}

void certify_authored_tangent_seam(const TopoDS_Face& a,const TopoDS_Face& b,
        double tolerance,std::size_t row,std::size_t other) {
    BRepBuilderAPI_Sewing sewing(tolerance);sewing.Add(a);sewing.Add(b);sewing.Perform();
    const auto joined=sewing.SewedShape();
    if(joined.IsNull()||joined.ShapeType()!=TopAbs_SHELL||sewing.NbMultipleEdges()!=0||!BRepCheck_Analyzer(joined).IsValid())
        fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
    const auto first=only_face(sewing.IsModified(a)?sewing.Modified(a):a,row,other);
    const auto second=only_face(sewing.IsModified(b)?sewing.Modified(b):b,row,other);
    BRepLib::EncodeRegularity(joined,1e-8);
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(first,TopAbs_EDGE,edges);
    occ::handle<TopTools_HSequenceOfShape> shared=new TopTools_HSequenceOfShape;
    for(TopExp_Explorer it(second,TopAbs_EDGE);it.More();it.Next())if(edges.Contains(it.Current())) {
        const auto edge=TopoDS::Edge(it.Current());
        if(BRep_Tool::Continuity(edge,first,second)<GeomAbs_G1)
            fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
        shared->Append(edge);
    }
    if(shared->IsEmpty())fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
    occ::handle<TopTools_HSequenceOfShape> wires=new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(shared,tolerance,false,wires);
    if(wires->Length()!=1)fail(OrderedLimitFailure::AmbiguousAdjacency,row,other);
}
} // namespace

OrderedLimitError::OrderedLimitError(OrderedLimitFailure code,std::size_t first,std::size_t second)
    :OrderedUpToError(code,first,second) {}

OrderedLimitResult build_ordered_up_to(const std::vector<OrderedLimitInput>& inputs,
        const Bnd_Box& envelope,double tolerance) {
    if(inputs.empty()||envelope.IsVoid()||envelope.IsOpen()||
            !std::isfinite(tolerance)||tolerance<Precision::Confusion())
        fail(OrderedLimitFailure::InvalidInput,0,0);
    if(inputs.size()==1) {
        if(inputs[0].authored_face.IsNull())fail(OrderedLimitFailure::InvalidInput,0,0);
        auto face=TopoDS::Face(BRepBuilderAPI_Copy(inputs[0].authored_face,true,false).Shape());
        face=extend_support(face,envelope,tolerance,0);
        OrderedLimitResult result;result.patches.push_back({face,{},{}});
        BRep_Builder builder;builder.MakeShell(result.shell);builder.Add(result.shell,face);return result;
    }
    std::vector<TopoDS_Face> authored,supports;std::vector<TopoDS_Solid> positives;
    for(std::size_t row=0;row<inputs.size();++row) {
        if(inputs[row].authored_face.IsNull())fail(OrderedLimitFailure::InvalidInput,row,row);
        // OCCT inspection/Boolean routines may populate topology flags and
        // surface caches even in non-destructive mode. Keep those local too.
        authored.push_back(TopoDS::Face(BRepBuilderAPI_Copy(inputs[row].authored_face,true,false).Shape()));
        supports.push_back(extend_support(authored.back(),envelope,tolerance,row));
    }
    std::vector<Adjacency> adjacency(inputs.size()-1);
    for(std::size_t row=1;row<inputs.size();++row) {
        adjacency[row-1]=check_adjacency(supports[row-1],supports[row],tolerance,row-1,row);
    }
    for(std::size_t row=1;row<inputs.size();++row) {
        if(adjacency[row-1].tangent) {
            // Normal-side choices from a transverse intersection cannot be
            // reinterpreted as a tangent seam's parameter-domain choice.
            if(inputs[row-1].keep_next_positive||inputs[row].keep_previous_positive)
                fail(OrderedLimitFailure::AmbiguousAdjacency,row-1,row);
            if(adjacency[row-1].line&&extend_analytic_tangent_line(supports[row-1],supports[row],authored[row-1],authored[row],
                    *adjacency[row-1].line,envelope,tolerance,row-1,row))continue;
            certify_authored_tangent_seam(authored[row-1],authored[row],tolerance,row-1,row);
            for(const auto index:{row-1,row}) {
                BRepAlgoAPI_Common domain(supports[index],authored[index]);domain.SetNonDestructive(true);domain.Build();
                if(!domain.IsDone())fail(OrderedLimitFailure::EmptyPatch,index,index);
                supports[index]=only_face(domain.Shape(),index,index);
            }
        }
    }
    Bnd_Box all_supports;
    for(const auto& support:supports)BRepBndLib::AddOptimal(support,all_supports,false,false);
    all_supports.Enlarge(10*tolerance);
    for(std::size_t row=0;row<supports.size();++row)
        positives.push_back(positive_half(supports[row],all_supports,tolerance,row));
    OrderedLimitResult result;
    for(std::size_t row=0;row<inputs.size();++row) {
        OrderedLimitPatch patch{supports[row],inputs[row].keep_previous_positive,inputs[row].keep_next_positive};
        if(row&&!adjacency[row-1].tangent)patch.face=retain(patch.face,authored[row],positives[row-1],
            patch.keep_previous_positive,tolerance,row,row-1);
        if(row+1<inputs.size()&&!adjacency[row].tangent)patch.face=retain(patch.face,authored[row],positives[row+1],
            patch.keep_next_positive,tolerance,row,row+1);
        result.patches.push_back(std::move(patch));
    }
    BRepBuilderAPI_Sewing sewing(tolerance);
    for(const auto& patch:result.patches)sewing.Add(patch.face);
    sewing.Perform();const auto joined=sewing.SewedShape();
    if(joined.IsNull()||joined.ShapeType()!=TopAbs_SHELL||
            sewing.NbMultipleEdges()!=0||!BRepCheck_Analyzer(joined).IsValid())
        fail(OrderedLimitFailure::DisconnectedChain,0,inputs.size()-1);
    // Every adjacent pair must share a real seam after trimming and sewing.
    // Each source row keeps its own descendant; runtime enumeration gives no ID.
    for(std::size_t row=0;row<result.patches.size();++row) {
        const auto& before=result.patches[row].face;
        const auto after=sewing.IsModified(before)?sewing.Modified(before):before;
        result.patches[row].face=only_face(after,row,row);
    }
    std::vector<Bnd_Box> bounds(result.patches.size());
    for(std::size_t row=0;row<result.patches.size();++row) {
        BRepBndLib::AddOptimal(result.patches[row].face,bounds[row],false,false);bounds[row].Enlarge(tolerance);
    }
    for(std::size_t a=0;a<result.patches.size();++a)for(std::size_t b=a+1;b<result.patches.size();++b) {
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(result.patches[a].face,TopAbs_EDGE,edges);
        bool shared=false;
        for(TopExp_Explorer edge(result.patches[b].face,TopAbs_EDGE);edge.More();edge.Next())
            shared|=edges.Contains(edge.Current());
        if(b==a+1&&!shared)fail(OrderedLimitFailure::DisconnectedChain,a,b);
        if(b>a+1&&!bounds[a].IsOut(bounds[b])) {
            BRepAlgoAPI_Section section(result.patches[a].face,result.patches[b].face,false);
            section.SetFuzzyValue(tolerance);section.Build();
            if(!section.IsDone()||TopExp_Explorer(section.Shape(),TopAbs_EDGE).More())
                fail(OrderedLimitFailure::SelfIntersection,a,b);
        }
    }
    result.shell=TopoDS::Shell(joined);return result;
}

OrderedLimitResult transform_ordered_up_to(const OrderedLimitResult& source,const gp_Trsf& transform) {
    BRepBuilderAPI_Transform moved(source.shell,transform,true);
    OrderedLimitResult result{TopoDS::Shell(moved.Shape()),source.patches};
    for(std::size_t row=0;row<result.patches.size();++row)
        result.patches[row].face=only_face(moved.ModifiedShape(source.patches[row].face),row,row);
    return result;
}
} // namespace zima::kernel::detail
