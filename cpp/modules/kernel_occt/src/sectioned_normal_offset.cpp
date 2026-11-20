#include "sectioned_normal_offset.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineSurfaceToBezierSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <math_SVD.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <source_location>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace zima::kernel::detail {
namespace {
struct Unavailable { unsigned line{}; };
void require(bool value,const std::source_location location=std::source_location::current()) {
    if (!value) throw Unavailable{location.line()};
}
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
struct Sample { gp_Pnt point; gp_Pnt2d first, second; };
struct Track {
    Handle(Geom_BSplineCurve) curve;
    Handle(Geom2d_BSplineCurve) first, second;
};
struct Support {
    TopoDS_Face source, result;
    Handle(Geom_Surface) surface;
    double u0{}, u1{}, v0{}, v1{};
    double v_scale() const { return v1-v0; }
};

Track fit_track(const std::function<Sample(double)>& evaluate,
    const std::function<double(const Sample&, double)>& residual, double tolerance) {
    std::map<double, Sample> points;
    for (unsigned i=0; i<=16; ++i) points.emplace(i/16., evaluate(i/16.));
    for (unsigned iteration=0; iteration<30; ++iteration) {
        const int count=static_cast<int>(points.size());
        Handle(NCollection_HArray1<gp_Pnt>) xyz=new NCollection_HArray1<gp_Pnt>(1,count);
        Handle(NCollection_HArray1<gp_Pnt2d>) a=new NCollection_HArray1<gp_Pnt2d>(1,count),
            b=new NCollection_HArray1<gp_Pnt2d>(1,count);
        Handle(NCollection_HArray1<double>) parameters=new NCollection_HArray1<double>(1,count);
        int index=1;
        for (const auto& [t,p]:points) {
            xyz->SetValue(index,p.point); a->SetValue(index,p.first);
            b->SetValue(index,p.second); parameters->SetValue(index,t); ++index;
        }
        GeomAPI_Interpolate curve(xyz,parameters,false,1e-12);
        Geom2dAPI_Interpolate first(a,parameters,false,1e-14), second(b,parameters,false,1e-14);
        curve.Perform(); first.Perform(); second.Perform();
        require(curve.IsDone()&&first.IsDone()&&second.IsDone());
        Track track{curve.Curve(),first.Curve(),second.Curve()};
        std::map<double,Sample> added;
        for (auto previous=points.begin(),next=std::next(previous); next!=points.end(); ++previous,++next) {
            for (double fraction:{.25,.5,.75}) {
                const double t=std::lerp(previous->first,next->first,fraction);
                const Sample p{track.curve->Value(t),track.first->Value(t),track.second->Value(t)};
                if (residual(p,t)>tolerance*.1) added.emplace(t,evaluate(t));
            }
        }
        if (added.empty()) return track;
        points.merge(added); require(points.size()<=15000);
    }
    throw Unavailable{};
}

// March in the mean *surface parameter*, rather than in a fixed world plane.
// A valid intersection can turn relative to a world plane near a tangent
// junction; solving against that plane can lose the intersection branch.
Sample intersection(const Support& a, const Support& b, double station,
    const gp_Pnt2d& seed_a, const gp_Pnt2d& seed_b, double scale, double tolerance) {
    std::array<double,4> x{seed_a.X(),seed_a.Y(),seed_b.X(),seed_b.Y()};
    const auto station_error=[&](const auto& q) {
        return scale*(station-((q[1]-a.v0)/a.v_scale()+(q[3]-b.v0)/b.v_scale())*.5);
    };
    for (unsigned iteration=0; iteration<80; ++iteration) {
        gp_Pnt pa,pb; gp_Vec au,av,bu,bv;
        a.surface->D1(x[0],x[1],pa,au,av); b.surface->D1(x[2],x[3],pb,bu,bv);
        const gp_Vec delta(pa,pb);
        const double axial=station_error(x), error=std::max(delta.Magnitude(),std::abs(axial));
        if (error<tolerance*.01) return {pa,{x[0],x[1]},{x[2],x[3]}};
        math_Matrix matrix(1,4,1,4); math_Vector rhs(1,4),step(1,4);
        for (int row=1; row<=3; ++row) {
            matrix(row,1)=au.Coord(row); matrix(row,2)=av.Coord(row);
            matrix(row,3)=-bu.Coord(row); matrix(row,4)=-bv.Coord(row); rhs(row)=delta.Coord(row);
        }
        matrix(4,1)=matrix(4,3)=0.; matrix(4,2)=scale*.5/a.v_scale();
        matrix(4,4)=scale*.5/b.v_scale(); rhs(4)=axial;
        math_SVD decomposition(matrix); require(decomposition.IsDone());
        decomposition.Solve(rhs,step,1e-12);
        bool improved=false;
        for (double damping=1.; damping>=1./1024.; damping*=.5) {
            auto next=x;
            for (unsigned i=0; i<4; ++i) next[i]+=step(i+1)*damping;
            const auto qa=a.surface->Value(next[0],next[1]),qb=b.surface->Value(next[2],next[3]);
            if (std::max(qa.Distance(qb),std::abs(station_error(next)))<error) {
                x=next; improved=true; break;
            }
        }
        require(improved);
    }
    throw Unavailable{};
}

double plane_value(const gp_Pln& plane,const gp_Pnt& point) {
    return gp_Vec(plane.Location(),point).Dot(gp_Vec(plane.Axis().Direction()));
}
gp_Pnt2d plane_coordinates(const gp_Pln& plane,const gp_Pnt& point) {
    const gp_Vec relative(plane.Location(),point);
    return {relative.Dot(gp_Vec(plane.Position().XDirection())),
        relative.Dot(gp_Vec(plane.Position().YDirection()))};
}

SectionedNormalOffset build_offset(const TopoDS_Shape& source,
    std::array<TopoDS_Face,2> caps,double distance,
    std::array<double,2> cap_distances,double tolerance,double extension) {
    require(std::isfinite(distance)&&std::abs(distance)>0.&&tolerance>0.);
    ShapeMap source_faces; TopExp::MapShapes(source,TopAbs_FACE,source_faces);
    require(source_faces.Contains(caps[0])&&source_faces.Contains(caps[1])&&!caps[0].IsSame(caps[1]));
    std::array<gp_Pln,2> planes;
    for (unsigned end=0; end<2; ++end) {
        const BRepAdaptor_Surface support(caps[end]); require(support.GetType()==GeomAbs_Plane);
        planes[end]=support.Plane();
    }
    BRep_Builder builder; std::vector<Support> faces;
    for (const auto& shape:source_faces) {
        if (shape.IsSame(caps[0])||shape.IsSame(caps[1])) continue;
        Support face; face.source=TopoDS::Face(shape);
        ShapeMap edges; TopExp::MapShapes(shape,TopAbs_EDGE,edges); require(edges.Extent()==4);
        const auto spline=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(face.source));
        require(!spline.IsNull());
        BRepTools::UVBounds(face.source,face.u0,face.u1,face.v0,face.v1);
        require(face.u1>face.u0&&face.v1>face.v0);
        const auto first=spline->Value((face.u0+face.u1)*.5,face.v0);
        const auto last=spline->Value((face.u0+face.u1)*.5,face.v1);
        if (faces.empty()&&planes[1].Distance(first)<planes[0].Distance(first)) {
            std::swap(caps[0],caps[1]); std::swap(planes[0],planes[1]);
            std::swap(cap_distances[0],cap_distances[1]);
        }
        require(planes[0].Distance(first)<=tolerance&&planes[1].Distance(last)<=tolerance);
        GeomConvert_BSplineSurfaceToBezierSurface conversion(spline,face.u0,face.u1,face.v0,face.v1,1e-12);
        require(conversion.NbUPatches()==1&&conversion.NbVPatches()==1);
        auto bezier=conversion.Patch(1,1);
        // Exact polynomial extension preserves the existing surface and its
        // shared boundary derivatives. Independent fitted extensions do not.
        bezier->Segment(-extension,1.+extension,-extension,1.+extension);
        auto extended=GeomConvert::SurfaceToBSplineSurface(bezier);
        NCollection_Array1<double> u(1,extended->NbUKnots()),v(1,extended->NbVKnots());
        extended->UKnots(u); extended->VKnots(v);
        for (auto& value:u) value=face.u0+(face.u1-face.u0)*(-extension+(1.+2.*extension)*value);
        for (auto& value:v) value=face.v0+(face.v1-face.v0)*(-extension+(1.+2.*extension)*value);
        extended->SetUKnots(u); extended->SetVKnots(v);
        face.surface=new Geom_OffsetSurface(extended,
            face.source.Orientation()==TopAbs_REVERSED?-distance:distance);
        builder.MakeFace(face.result,face.surface,tolerance); faces.push_back(std::move(face));
    }
    require(faces.size()>=2);
    for (unsigned end=0; end<2; ++end) {
        auto normal=gp_Vec(planes[end].Axis().Direction());
        if (caps[end].Orientation()==TopAbs_REVERSED) normal.Reverse();
        planes[end].Translate(normal*cap_distances[end]);
    }
    struct Joint {
        unsigned a{},b{}; TopoDS_Edge source,result; Track track;
        std::array<TopoDS_Vertex,2> source_vertices,vertices;
    };
    std::vector<Joint> joints;
    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> adjacency;
    TopExp::MapShapesAndAncestors(source,TopAbs_EDGE,TopAbs_FACE,adjacency);
    std::vector<std::pair<TopoDS_Shape,TopoDS_Shape>> descendants;
    for (int index=1; index<=adjacency.Extent(); ++index) {
        const auto& incident=adjacency.FindFromIndex(index); require(incident.Size()==2);
        std::optional<unsigned> ai,bi;
        for (unsigned i=0; i<faces.size(); ++i) {
            if (faces[i].source.IsSame(incident.First())) ai=i;
            if (faces[i].source.IsSame(incident.Last())) bi=i;
        }
        if (!ai||!bi) continue;
        Joint joint; joint.a=*ai; joint.b=*bi; joint.source=TopoDS::Edge(adjacency.FindKey(index));
        joint.source.Orientation(TopAbs_FORWARD);
        const auto& a=faces[*ai]; const auto& b=faces[*bi];
        double first,last,other_first,other_last;
        const auto ca=BRep_Tool::CurveOnSurface(joint.source,a.source,first,last),
            cb=BRep_Tool::CurveOnSurface(joint.source,b.source,other_first,other_last);
        require(!ca.IsNull()&&!cb.IsNull()&&std::abs(first-other_first)<1e-10&&std::abs(last-other_last)<1e-10);
        joint.source_vertices={TopExp::FirstVertex(joint.source),TopExp::LastVertex(joint.source)};
        if (ca->Value(first).Y()>ca->Value(last).Y()) {
            std::swap(first,last); std::swap(joint.source_vertices[0],joint.source_vertices[1]);
        }
        const double scale=std::max(1.,BRep_Tool::Pnt(joint.source_vertices[0]).Distance(BRep_Tool::Pnt(joint.source_vertices[1])));
        const auto point_at=[&](double station) {
            return intersection(a,b,station,ca->Value(std::lerp(first,last,station)),
                cb->Value(std::lerp(first,last,station)),scale,tolerance);
        };
        const auto end_parameter=[&](unsigned end) {
            double low=end-extension*.8,high=end+extension*.8;
            const double sign=plane_value(planes[end],point_at(low).point);
            require(sign*plane_value(planes[end],point_at(high).point)<=0.);
            for (unsigned i=0; i<45; ++i) {
                const double middle=(low+high)*.5;
                if (sign*plane_value(planes[end],point_at(middle).point)>0.) low=middle;
                else high=middle;
            }
            return (low+high)*.5;
        };
        const double begin=end_parameter(0),end=end_parameter(1); require(end>begin);
        joint.track=fit_track([&](double t){return point_at(std::lerp(begin,end,t));},
            [&](const Sample& p,double t) {
                const double station=((p.first.Y()-a.v0)/a.v_scale()+(p.second.Y()-b.v0)/b.v_scale())*.5;
                return std::max({p.point.Distance(a.surface->Value(p.first.X(),p.first.Y())),
                    p.point.Distance(b.surface->Value(p.second.X(),p.second.Y())),
                    std::abs(station-std::lerp(begin,end,t))*scale});
            },tolerance);
        for (unsigned end_index=0; end_index<2; ++end_index)
            joint.vertices[end_index]=BRepBuilderAPI_MakeVertex(joint.track.curve->Value(end_index)).Vertex();
        joint.result=BRepBuilderAPI_MakeEdge(joint.track.curve,joint.vertices[0],joint.vertices[1]).Edge();
        builder.UpdateEdge(joint.result,joint.track.first,faces[*ai].result,tolerance);
        builder.UpdateEdge(joint.result,joint.track.second,faces[*bi].result,tolerance);
        descendants.emplace_back(joint.source,joint.result);
        for (unsigned end_index=0; end_index<2; ++end_index)
            descendants.emplace_back(joint.source_vertices[end_index],joint.vertices[end_index]);
        joints.push_back(std::move(joint));
    }
    require(joints.size()==faces.size());
    std::array<TopoDS_Face,2> result_caps;
    std::array<NCollection_List<TopoDS_Shape>,2> cap_edges;
    for (unsigned end=0; end<2; ++end) builder.MakeFace(result_caps[end],new Geom_Plane(planes[end]),tolerance);
    for (unsigned i=0; i<faces.size(); ++i) {
        auto& face=faces[i]; Joint *left=nullptr,*right=nullptr;
        for (auto& joint:joints) if (joint.a==i||joint.b==i) {
            const auto curve=joint.a==i?joint.track.first:joint.track.second;
            if (curve->Value(.5).X()<(face.u0+face.u1)*.5) {require(!left);left=&joint;}
            else {require(!right);right=&joint;}
        }
        require(left&&right);
        std::array<TopoDS_Edge,2> ends;
        for (unsigned end=0; end<2; ++end) {
            const auto a=(left->a==i?left->track.first:left->track.second)->Value(end),
                b=(right->a==i?right->track.first:right->track.second)->Value(end);
            const auto point_at=[&](double t) {
                const double u=std::lerp(a.X(),b.X(),t); double v=std::lerp(a.Y(),b.Y(),t);
                gp_Pnt point;
                for (unsigned step=0; step<30; ++step) {
                    gp_Vec du,dv; face.surface->D1(u,v,point,du,dv);
                    const double error=plane_value(planes[end],point);
                    if (std::abs(error)<tolerance*.001) return Sample{point,{u,v},plane_coordinates(planes[end],point)};
                    const double derivative=dv.Dot(gp_Vec(planes[end].Axis().Direction()));
                    require(std::abs(derivative)>1e-12); v-=error/derivative;
                }
                throw Unavailable{};
            };
            const auto track=fit_track(point_at,[&](const Sample& p,double) {
                const auto origin=planes[end].Location();
                const auto planar=origin.Translated(gp_Vec(planes[end].Position().XDirection())*p.second.X()+
                    gp_Vec(planes[end].Position().YDirection())*p.second.Y());
                return std::max(p.point.Distance(face.surface->Value(p.first.X(),p.first.Y())),p.point.Distance(planar));
            },tolerance);
            ends[end]=BRepBuilderAPI_MakeEdge(track.curve,left->vertices[end],right->vertices[end]).Edge();
            builder.UpdateEdge(ends[end],track.first,face.result,tolerance);
            builder.UpdateEdge(ends[end],track.second,result_caps[end],tolerance); cap_edges[end].Append(ends[end]);
            std::optional<TopoDS_Shape> parent;
            for (TopExp_Explorer e(face.source,TopAbs_EDGE); e.More(); e.Next()) {
                for (const auto& incident:adjacency.FindFromKey(e.Current())) if (incident.IsSame(caps[end])) {
                    require(!parent); parent=e.Current();
                }
            }
            require(parent.has_value()); descendants.emplace_back(*parent,ends[end]);
        }
        BRepBuilderAPI_MakeWire wire;
        wire.Add(ends[0]); wire.Add(right->result); wire.Add(TopoDS::Edge(ends[1].Reversed()));
        wire.Add(TopoDS::Edge(left->result.Reversed())); require(wire.IsDone());
        builder.Add(face.result,wire.Wire()); face.result.Orientation(face.source.Orientation());
        BRepLib::SameParameter(face.result,tolerance);
        descendants.emplace_back(face.source,face.result);
    }
    BRepBuilderAPI_Sewing sewing(tolerance);
    for (const auto& face:faces) sewing.Add(face.result);
    for (unsigned end=0; end<2; ++end) {
        BRepBuilderAPI_MakeWire wire; wire.Add(cap_edges[end]); require(wire.IsDone());
        // Each authored plane can have its own handed UV frame. Orient the
        // bounding wire in that frame instead of assuming both caps use +Z.
        BRepBuilderAPI_MakeFace cap(BRep_Tool::Surface(result_caps[end]),wire.Wire(),true);
        require(cap.IsDone()); result_caps[end]=cap.Face();
        BRepLib::SameParameter(result_caps[end],tolerance);
        sewing.Add(result_caps[end]); descendants.emplace_back(caps[end],result_caps[end]);
    }
    sewing.Perform(); require(sewing.NbFreeEdges()==0&&sewing.NbMultipleEdges()==0);
    require(sewing.SewedShape().ShapeType()==TopAbs_SHELL);
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
    require(BRepLib::OrientClosedSolid(solid));
    TopoDS_Solid forward; builder.MakeSolid(forward);
    for (TopoDS_Iterator child(solid); child.More(); child.Next()) builder.Add(forward,child.Value());
    require(BRepCheck_Analyzer(forward,true,false,true).IsValid()&&BRepAlgoAPI_Check(forward,false,true).IsValid());
    GProp_GProps properties; BRepGProp::VolumeProperties(forward,properties);
    require(std::isfinite(properties.Mass())&&properties.Mass()>0.);
    SectionedNormalOffset result{forward,new BRepTools_History};
    ShapeMap members; TopExp::MapShapes(forward,members);
    for (const auto& [parent,created]:descendants) {
        auto final=created;
        if (!members.Contains(final)&&sewing.IsModifiedSubShape(created)) final=sewing.ModifiedSubShape(created);
        if (!members.Contains(final)&&sewing.IsModified(created)) final=sewing.Modified(created);
        require(members.Contains(final)&&parent.ShapeType()==final.ShapeType());
        result.history->AddGenerated(parent,final);
    }
    return result;
}
}

std::optional<SectionedNormalOffset> sectioned_normal_offset(const TopoDS_Shape& source,
    const std::array<TopoDS_Face,2>& caps,double distance,
    const std::array<double,2>& cap_distances,double tolerance) {
    for (double extension:{.1,.25,.5}) {
        try { return build_offset(source,caps,distance,cap_distances,std::min(tolerance,1e-7),extension); }
        catch (const Unavailable& failure) {
            if (std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                std::fprintf(stderr,"Sectioned offset rejected at %u, extension %.2f\n",failure.line,extension);
        }
        catch (const Standard_Failure& failure) {
            if (std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                std::fprintf(stderr,"Sectioned offset: %s\n",failure.GetMessageString());
        }
    }
    return std::nullopt;
}
}
