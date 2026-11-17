#pragma once
#include <BRepFeat_SplitShape.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeFix_Face.hxx>
#include <GeomFill_Frenet.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>

// Private construction for routes whose support angle changes along the spine.
// Offset intersections define the centers; exact support contacts trim the
// radius envelope. The caller owns material operations, terminal choices and
// persistent ancestry. No complete OCCT Fillet builder is used here.
namespace zima::kernel::support_driven_fillet {
struct Support {
    TopoDS_Face face;
    occ::handle<Geom_Surface> surface;
    occ::handle<Geom_Surface> offset;
    bool collapsed{};
};
struct Segment {
    TopoDS_Edge source;
    std::array<Support,2> supports;
    occ::handle<Geom_Curve> centers;
    gp_Pnt start,end;
    double first{},last{};
    bool collapsed{};
};
struct Result { TopoDS_Shape solid;std::vector<TopoDS_Face> faces; };

inline gp_Dir normal(const TopoDS_Face& face,const gp_Pnt& point) {
    BRepAdaptor_Surface surface(face);
    gp_Dir result;
    if(surface.GetType()==GeomAbs_Plane) {
        result=surface.Plane().Axis().Direction();
        if(!surface.Plane().Position().Direct())result.Reverse();
    }
    else {
        const auto cylinder=surface.Cylinder();
        const gp_Vec axis(cylinder.Axis().Direction());
        const auto on_axis=cylinder.Location().Translated(axis*
            gp_Vec(cylinder.Location(),point).Dot(axis));
        result=gp_Dir(gp_Vec(on_axis,point));
        if(!cylinder.Position().Direct())result.Reverse();
    }
    if(face.Orientation()==TopAbs_REVERSED)result.Reverse();
    return result;
}
inline gp_Pnt center_guess(const Segment& segment,const gp_Pnt& point,
        double signed_radius) {
    const gp_Vec first(normal(segment.supports[0].face,point));
    const gp_Vec second(normal(segment.supports[1].face,point));
    return point.Translated((first+second)*
        (signed_radius/(1.+first.Dot(second))));
}
inline Support support(const TopoDS_Face& face,double distance) {
    BRepAdaptor_Surface surface(face);
    double signed_distance=face.Orientation()==TopAbs_REVERSED?-distance:distance;
    Support result;result.face=face;
    if(surface.GetType()==GeomAbs_Plane) {
        auto plane=surface.Plane();result.surface=new Geom_Plane(plane);
        if(!plane.Position().Direct())signed_distance=-signed_distance;
        plane.Translate(gp_Vec(plane.Axis().Direction())*signed_distance);
        result.offset=new Geom_Plane(plane);
    } else if(surface.GetType()==GeomAbs_Cylinder) {
        auto cylinder=surface.Cylinder();
        if(!cylinder.Position().Direct())signed_distance=-signed_distance;
        result.surface=new Geom_CylindricalSurface(cylinder);
        if(std::abs(cylinder.Radius()+signed_distance)<1.e-9) {
            result.collapsed=true;return result;
        }
        if(cylinder.Radius()+signed_distance<=0.)
            throw std::runtime_error("Rolling route offset cylinder collapsed");
        cylinder.SetRadius(cylinder.Radius()+signed_distance);
        result.offset=new Geom_CylindricalSurface(cylinder);
    } else throw std::runtime_error("Rolling route requires analytic supports");
    return result;
}
inline bool same(const Support& a,const Support& b,double tolerance) {
    if(a.face.IsSame(b.face))return true;
    const BRepAdaptor_Surface x(a.face),y(b.face);
    if(x.GetType()!=y.GetType())return false;
    if(x.GetType()==GeomAbs_Plane)
        return x.Plane().Axis().Direction().IsParallel(y.Plane().Axis().Direction(),1.e-9)&&
            x.Plane().Distance(y.Plane().Location())<tolerance;
    const auto c=x.Cylinder(),d=y.Cylinder();
    return std::abs(c.Radius()-d.Radius())<tolerance&&
        c.Axis().Direction().IsParallel(d.Axis().Direction(),1.e-9)&&
        gp_Lin(c.Axis()).Distance(d.Location())<tolerance;
}
inline gp_Pnt joint(const Segment& a,const Segment& b,const gp_Pnt& point,
        double signed_radius,double tolerance) {
    if(a.collapsed)return a.start;
    if(b.collapsed)return b.start;
    const auto guess=center_guess(a,point,signed_radius);
    GeomAPI_ProjectPointOnCurve first_projection(guess,a.centers),second_projection(guess,b.centers);
    if(first_projection.NbPoints()&&second_projection.NbPoints()&&
       first_projection.LowerDistance()<tolerance&&second_projection.LowerDistance()<tolerance)
        return guess;
    for(std::size_t i=0;i<2;++i)for(std::size_t j=0;j<2;++j) {
        if(!same(a.supports[i],b.supports[j],tolerance))continue;
        if(same(a.supports[1-i],b.supports[1-j],tolerance)) {
            GeomAPI_ProjectPointOnCurve projection(guess,a.centers);
            if(projection.NbPoints())return projection.NearestPoint();
        }
        const auto& previous=a.supports[1-i];
        const auto& next=b.supports[1-j];
        const auto& round=BRepAdaptor_Surface(previous.face).GetType()==GeomAbs_Cylinder
            ?previous:next;
        if(BRepAdaptor_Surface(round.face).GetType()!=GeomAbs_Cylinder)continue;
        const auto cylinder=BRepAdaptor_Surface(round.face).Cylinder();
        const gp_Pnt seam=point.Translated(gp_Vec(normal(round.face,point))*signed_radius);
        occ::handle<Geom_Line> line=new Geom_Line(seam,cylinder.Axis().Direction());
        GeomAPI_IntCS intersection(line,a.supports[i].offset);
        if(!intersection.IsDone())continue;
        double distance=std::numeric_limits<double>::infinity();gp_Pnt best;
        for(int k=1;k<=intersection.NbPoints();++k)
            if(intersection.Point(k).Distance(guess)<distance) {
                best=intersection.Point(k);distance=best.Distance(guess);
            }
        if(std::isfinite(distance))return best;
    }
    throw std::runtime_error("Rolling route has no common support at a transition");
}
inline TopoDS_Shape boolean(const TopoDS_Shape& shape,const TopoDS_Shape& tool,
        bool cut,double tolerance) {
    BRepAlgoAPI_BooleanOperation operation;
    operation.SetOperation(cut?BOPAlgo_CUT:BOPAlgo_COMMON);
    operation.SetArguments({shape});operation.SetTools({tool});
    operation.SetNonDestructive(true);operation.SetFuzzyValue(tolerance);
    operation.Build();
    if(!operation.IsDone()||operation.Shape().IsNull()||
       !BRepCheck_Analyzer(operation.Shape()).IsValid())
        throw std::runtime_error("Rolling route support trim failed");
    return operation.Shape();
}
inline TopoDS_Shape clip(const TopoDS_Shape& shape,
        const occ::handle<Geom_Surface>& surface,const gp_Pnt& inside,double tolerance) {
    if(const auto cylinder=occ::down_cast<Geom_CylindricalSurface>(surface)) {
        Bnd_Box bounds;BRepBndLib::Add(shape,bounds);
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        const auto geometry=cylinder->Cylinder();
        const gp_Vec axis(geometry.Axis().Direction());
        double lo=1.e100,hi=-1.e100;
        for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1}) {
            double t=gp_Vec(geometry.Location(),gp_Pnt(x,y,z)).Dot(axis);
            lo=std::min(lo,t);hi=std::max(hi,t);
        }
        const gp_Ax2 frame(geometry.Location().Translated(axis*(lo-1.)),
            geometry.Axis().Direction(),geometry.Position().XDirection());
        auto region=BRepPrimAPI_MakeCylinder(frame,geometry.Radius(),hi-lo+2.).Solid();
        return boolean(shape,region,gp_Lin(geometry.Axis()).Distance(inside)>geometry.Radius(),tolerance);
    }
    auto face=BRepBuilderAPI_MakeFace(surface,tolerance).Face();
    return boolean(shape,BRepPrimAPI_MakeHalfSpace(face,inside).Solid(),false,tolerance);
}
inline gp_Pnt section_corner(const Segment& item,const gp_Pnt& center,
        const gp_Dir& axis,gp_Pnt point,double tolerance) {
    for(int iteration=0;iteration<32;++iteration) {
        std::array<gp_Vec,3> normals;std::array<double,3> errors;
        for(std::size_t i=0;i<2;++i) {
            BRepAdaptor_Surface surface(item.supports[i].face);
            if(surface.GetType()==GeomAbs_Plane) {
                normals[i]=gp_Vec(surface.Plane().Axis().Direction());
                errors[i]=gp_Vec(surface.Plane().Location(),point).Dot(normals[i]);
            } else {
                auto cylinder=surface.Cylinder();const gp_Vec a(cylinder.Axis().Direction());
                auto radial=gp_Vec(cylinder.Location(),point);radial-=a*radial.Dot(a);
                errors[i]=radial.Magnitude()-cylinder.Radius();normals[i]=radial.Normalized();
            }
        }
        normals[2]=gp_Vec(axis);errors[2]=gp_Vec(center,point).Dot(normals[2]);
        if(std::max({std::abs(errors[0]),std::abs(errors[1]),std::abs(errors[2])})<tolerance)return point;
        const double determinant=normals[0].Dot(normals[1].Crossed(normals[2]));
        if(std::abs(determinant)<1.e-12)break;
        const auto step=(normals[1].Crossed(normals[2])*errors[0]+normals[2].Crossed(normals[0])*errors[1]+normals[0].Crossed(normals[1])*errors[2])/determinant;
        point.Translate(-step);
    }
    throw std::runtime_error("Source section corner did not converge");
}
inline Result build(std::span<const TopoDS_Edge> ordered,
        std::span<const std::array<TopoDS_Face,2>> faces,
        double radius,bool adds,double tolerance) {
    // Contact construction needs a tighter tolerance than the document's
    // Boolean merge allowance; the latter may be one micron or larger.
    tolerance=std::min(tolerance,1.e-7);
    const double signed_radius=adds?radius:-radius;
    std::vector<Segment> segments;
    for(std::size_t i=0;i<ordered.size();++i) {
        Segment item;item.source=ordered[i];
        item.supports={support(faces[i][0],signed_radius),support(faces[i][1],signed_radius)};
        BRepAdaptor_Curve guide(item.source);
        auto middle=guide.Value(.5*(guide.FirstParameter()+guide.LastParameter()));
        const auto guess=center_guess(item,middle,signed_radius);
        if(item.supports[0].collapsed||item.supports[1].collapsed) {
            const auto index=item.supports[0].collapsed?0:1;
            const auto cylinder=BRepAdaptor_Surface(item.supports[index].face).Cylinder();
            if(item.supports[1-index].collapsed)throw std::runtime_error("Both rolling supports collapsed");
            GeomAPI_IntCS intersection(new Geom_Line(cylinder.Axis()),item.supports[1-index].offset);
            if(!intersection.IsDone()||intersection.NbPoints()!=1)throw std::runtime_error("Collapsed rolling support has no unique center");
            item.start=item.end=intersection.Point(1);item.collapsed=true;
            segments.push_back(std::move(item));continue;
        }
        GeomAPI_IntSS intersection(item.supports[0].offset,item.supports[1].offset,std::min(tolerance,1.e-10));
        if(!intersection.IsDone())throw std::runtime_error("Rolling route offset intersection failed");
        double nearest=1.e100;
        for(int k=1;k<=intersection.NbLines();++k) {
            GeomAPI_ProjectPointOnCurve projection(guess,intersection.Line(k));
            if(projection.NbPoints()&&projection.LowerDistance()<nearest) {
                nearest=projection.LowerDistance();item.centers=intersection.Line(k);
            }
        }
        if(item.centers.IsNull())throw std::runtime_error("Rolling route has no center line");
        // IntSS may return a closed, non-periodic spline. A route crossing its
        // seam must keep the short authored branch instead of the other turn.
        if(item.centers->IsClosed()&&!item.centers->IsPeriodic())
            if(const auto spline=occ::down_cast<Geom_BSplineCurve>(item.centers))spline->SetPeriodic();
        TopoDS_Vertex a,b;TopExp::Vertices(item.source,a,b,true);
        GeomAPI_ProjectPointOnCurve start(center_guess(item,BRep_Tool::Pnt(a),signed_radius),item.centers);
        GeomAPI_ProjectPointOnCurve end(center_guess(item,BRep_Tool::Pnt(b),signed_radius),item.centers);
        if(!start.NbPoints()||!end.NbPoints())throw std::runtime_error("Rolling route endpoint projection failed");
        item.start=start.NearestPoint();item.end=end.NearestPoint();
        segments.push_back(std::move(item));
    }
    for(std::size_t i=1;i<segments.size();++i) {
        TopoDS_Vertex a,b;TopExp::Vertices(ordered[i],a,b,true);
        const auto center=joint(segments[i-1],segments[i],BRep_Tool::Pnt(a),signed_radius,tolerance);
        segments[i-1].end=center;segments[i].start=center;
    }
    if(segments.size()>1) {
        TopoDS_Vertex first,last,ignored;
        TopExp::Vertices(ordered.front(),first,ignored,true);
        TopExp::Vertices(ordered.back(),ignored,last,true);
        if(first.IsSame(last)) {
            const auto center=joint(segments.back(),segments.front(),BRep_Tool::Pnt(first),signed_radius,tolerance);
            segments.back().end=center;segments.front().start=center;
        }
    }
    Result result;
    for(auto& item:segments) {
        if(item.collapsed) {
            const std::size_t index=&item-segments.data();
            if(index==0||index+1==segments.size())throw std::runtime_error("Collapsed rolling support needs adjacent sections");
            const auto center=item.start;
            gp_Pnt ignored;gp_Vec previous_tangent,next_tangent;
            GeomAPI_ProjectPointOnCurve previous(center,segments[index-1].centers),next(center,segments[index+1].centers);
            segments[index-1].centers->D1(previous.LowerDistanceParameter(),ignored,previous_tangent);
            segments[index+1].centers->D1(next.LowerDistanceParameter(),ignored,next_tangent);
            BRepAdaptor_Curve source(item.source);const auto corner=source.Value(.5*(source.FirstParameter()+source.LastParameter()));
            const auto inside=corner.Translated(gp_Vec(corner,center)*.05);
            TopoDS_Shape cell=BRepPrimAPI_MakeBox(center.Translated(gp_Vec(-4.*radius,-4.*radius,-4.*radius)),8.*radius,8.*radius,8.*radius).Shape();
            cell=clip(cell,new Geom_Plane(center,gp_Dir(previous_tangent)),inside,tolerance);
            cell=clip(cell,new Geom_Plane(center,gp_Dir(next_tangent)),inside,tolerance);
            for(const auto& surface:item.supports)cell=clip(cell,surface.surface,inside,tolerance);
            auto ball=BRepPrimAPI_MakeSphere(center,radius).Shape();
            BRepAlgoAPI_Cut cut;cut.SetArguments({cell});cut.SetTools({ball});cut.SetNonDestructive(true);cut.SetFuzzyValue(tolerance);cut.Build();
            if(!cut.IsDone()||!BRepCheck_Analyzer(cut.Shape()).IsValid())throw std::runtime_error("Collapsed rolling sphere trim failed");
            Result component;
            for(TopExp_Explorer it(cut.Shape(),TopAbs_SOLID);it.More();it.Next()) {
                BRepClass3d_SolidClassifier classifier(it.Current(),inside,tolerance);
                if(classifier.State()==TopAbs_IN){component.solid=it.Current();break;}
            }
            if(component.solid.IsNull())throw std::runtime_error("Collapsed rolling sphere corner missing");
            for(TopExp_Explorer it(component.solid,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());
                if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Sphere)component.faces.push_back(face);
            }
            if(component.faces.empty())throw std::runtime_error("Collapsed rolling sphere surface missing");
            BRepAlgoAPI_Fuse fuse;fuse.SetArguments({result.solid});fuse.SetTools({component.solid});fuse.SetNonDestructive(true);fuse.SetFuzzyValue(tolerance);fuse.Build();
            if(!fuse.IsDone()||!BRepCheck_Analyzer(fuse.Shape()).IsValid())throw std::runtime_error("Collapsed rolling sphere join failed");
            result.faces.insert(result.faces.end(),component.faces.begin(),component.faces.end());
            TopTools_IndexedMapOfShape final_faces;TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,final_faces);
            std::vector<TopoDS_Face> mapped;
            for(const auto& face:result.faces) {
                if(final_faces.Contains(face))mapped.push_back(face);
                for(const auto& changed:fuse.Modified(face))if(changed.ShapeType()==TopAbs_FACE&&final_faces.Contains(changed))mapped.push_back(TopoDS::Face(changed));
            }
            result.solid=fuse.Shape();result.faces=std::move(mapped);continue;
        }
        GeomAPI_ProjectPointOnCurve start(item.start,item.centers),end(item.end,item.centers);
        if(!start.NbPoints()||!end.NbPoints()||start.LowerDistance()>20.*tolerance||end.LowerDistance()>20.*tolerance)
            throw std::runtime_error("Rolling route joint misses its offset supports");
        item.first=start.LowerDistanceParameter();item.last=end.LowerDistanceParameter();
        BRepAdaptor_Curve original(item.source);
        gp_Pnt ignored;gp_Vec direction,guide_direction;
        item.centers->D1(item.first,ignored,direction);
        original.D1(item.source.Orientation()==TopAbs_REVERSED?original.LastParameter():original.FirstParameter(),ignored,guide_direction);
        if(item.source.Orientation()==TopAbs_REVERSED)guide_direction.Reverse();
        const bool increasing=direction.Dot(guide_direction)>0.;
        if(item.centers->IsPeriodic()) {
            if(increasing&&item.last<item.first)item.last+=item.centers->Period();
            if(!increasing&&item.last>item.first)item.last-=item.centers->Period();
        }
        const int pieces=item.centers->IsPeriodic()?std::max(1,int(std::ceil(std::abs(item.last-item.first)/(std::numbers::pi/3.)))):1;
        for(int part=0;part<pieces;++part) {
            const double u=item.first+(item.last-item.first)*part/pieces;
            const double v=item.first+(item.last-item.first)*(part+1)/pieces;
            auto rail=BRepBuilderAPI_MakeEdge(item.centers,std::min(u,v),std::max(u,v)).Edge();
            if(v<u)rail.Reverse();
            gp_Pnt first,last;gp_Vec tangent_first,tangent_last;
            item.centers->D1(u,first,tangent_first);item.centers->D1(v,last,tangent_last);
            if(v<u){tangent_first.Reverse();tangent_last.Reverse();}
            std::optional<gp_Dir> binormal;
            for(const auto& surface:item.supports)
                if(BRepAdaptor_Surface(surface.face).GetType()==GeomAbs_Plane)
                    binormal=normal(surface.face,first);
            gp_Ax2 circle_frame(first,gp_Dir(tangent_first));
            TopoDS_Edge circle;
            TopoDS_Wire section;
            if(binormal) {
                circle_frame.SetXDirection(*binormal);
                std::optional<double> anchor_angle;double lo=1.e100,hi=-1.e100;
                for(int sample=0;sample<=20;++sample) {
                    const double parameter=u+(v-u)*sample/20.;
                    gp_Pnt center;gp_Vec tangent;item.centers->D1(parameter,center,tangent);
                    if(v<u)tangent.Reverse();
                    const gp_Vec y=gp_Vec(gp_Dir(tangent)).Crossed(gp_Vec(*binormal));
                    for(const auto& surface:item.supports) {
                        auto radial=gp_Vec(normal(surface.face,center));
                        if(adds)radial.Reverse();
                        double angle=std::atan2(radial.Dot(y),radial.Dot(gp_Vec(*binormal)));
                        if(anchor_angle)angle+=2.*std::numbers::pi*std::round((*anchor_angle-angle)/(2.*std::numbers::pi));
                        else anchor_angle=angle;
                        lo=std::min(lo,angle);hi=std::max(hi,angle);
                    }
                }
                lo-=.05;hi+=.05;
                circle=BRepBuilderAPI_MakeEdge(gp_Circ(circle_frame,radius),lo,hi).Edge();
                TopoDS_Vertex a,b;TopExp::Vertices(circle,a,b,true);
                BRepBuilderAPI_MakeWire wire;wire.Add(circle);
                wire.Add(BRepBuilderAPI_MakeEdge(BRep_Tool::Pnt(b),first).Edge());
                wire.Add(BRepBuilderAPI_MakeEdge(first,BRep_Tool::Pnt(a)).Edge());
                section=wire.Wire();
            } else {
                GeomFill_Frenet law;law.SetCurve(new GeomAdaptor_Curve(item.centers,std::min(u,v),std::max(u,v)));
                gp_Vec tangent,n,b;law.D0(u,tangent,n,b);
                circle_frame.SetXDirection(gp_Dir(n));
                std::optional<double> anchor_angle;double lo=1.e100,hi=-1.e100;
                for(int sample=0;sample<=20;++sample) {
                    const double parameter=u+(v-u)*sample/20.;
                    law.D0(parameter,tangent,n,b);if(v<u)b.Reverse();
                    const auto center=item.centers->Value(parameter);
                    for(const auto& surface:item.supports) {
                        auto radial=gp_Vec(normal(surface.face,center));if(adds)radial.Reverse();
                        double angle=std::atan2(radial.Dot(b),radial.Dot(n));
                        if(anchor_angle)angle+=2.*std::numbers::pi*std::round((*anchor_angle-angle)/(2.*std::numbers::pi));
                        else anchor_angle=angle;
                        lo=std::min(lo,angle);hi=std::max(hi,angle);
                    }
                }
                lo-=.05;hi+=.05;
                circle=BRepBuilderAPI_MakeEdge(gp_Circ(circle_frame,radius),lo,hi).Edge();
                TopoDS_Vertex a,bv;TopExp::Vertices(circle,a,bv,true);
                BRepBuilderAPI_MakeWire wire;wire.Add(circle);
                wire.Add(BRepBuilderAPI_MakeEdge(BRep_Tool::Pnt(bv),first).Edge());
                wire.Add(BRepBuilderAPI_MakeEdge(first,BRep_Tool::Pnt(a)).Edge());
                section=wire.Wire();
            }
            BRepOffsetAPI_MakePipeShell pipe(BRepBuilderAPI_MakeWire(rail).Wire());
            // The envelope must agree with its analytic contact curves more
            // accurately than the final Boolean merge allowance. Otherwise a
            // tangent join can retain a narrow support flap or an open seam.
            pipe.SetTolerance(std::min(tolerance,1.e-10),std::min(tolerance,1.e-10),1.e-9);
            if(binormal)pipe.SetMode(*binormal);else pipe.SetMode(true);
            TopoDS_Vertex rail_start,rail_end;TopExp::Vertices(rail,rail_start,rail_end,true);
            pipe.Add(section,rail_start,false,false);pipe.Build();
            if(!pipe.IsDone()||!pipe.MakeSolid())throw std::runtime_error("Rolling route tube failed");
            TopoDS_Shape pipe_shape=pipe.Shape();
            std::vector<std::pair<TopoDS_Edge,std::size_t>> contact_edges;
            std::vector<TopoDS_Face> lateral_faces;
            for(const auto& source:pipe.Generated(circle))if(source.ShapeType()==TopAbs_FACE)
                lateral_faces.push_back(TopoDS::Face(source));
            if(lateral_faces.size()!=1)throw std::runtime_error("Rolling route has no unique envelope surface");
            {
                BRepFeat_SplitShape split(pipe_shape);
                for(std::size_t support_index=0;support_index<2;++support_index) {
                    const auto& support=item.supports[support_index];
                    auto contact=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(item.centers,std::min(u,v),std::max(u,v)));
                    for(int pole=1;pole<=contact->NbPoles();++pole) {
                        auto point=contact->Pole(pole);
                        if(const auto cylinder=occ::down_cast<Geom_CylindricalSurface>(support.surface)) {
                            const auto offset=occ::down_cast<Geom_CylindricalSurface>(support.offset);
                            const auto axis=cylinder->Axis();const gp_Vec direction(axis.Direction());
                            const auto axis_point=axis.Location().Translated(direction*gp_Vec(axis.Location(),point).Dot(direction));
                            point=axis_point.Translated(gp_Vec(axis_point,point)*(cylinder->Radius()/offset->Radius()));
                        } else point.Translate(gp_Vec(normal(support.face,point))*(-signed_radius));
                        contact->SetPole(pole,point);
                    }
                    auto edge=BRepBuilderAPI_MakeEdge(contact).Edge();
                    {
                        BRep_Builder builder;double maximum=tolerance;
                        for(TopExp_Explorer vertices(edge,TopAbs_VERTEX);vertices.More();vertices.Next()) {
                            const auto vertex=TopoDS::Vertex(vertices.Current());
                            BRepExtrema_DistShapeShape d0(vertex,pipe.FirstShape()),d1(vertex,pipe.LastShape());
                            const double gap=std::min(d0.Value(),d1.Value());
                            const double vertex_tolerance=std::max(tolerance,2.*gap);
                            builder.UpdateVertex(vertex,vertex_tolerance);maximum=std::max(maximum,vertex_tolerance);
                        }
                        builder.UpdateEdge(edge,maximum);
                    }
                    ShapeFix_Edge fix;fix.FixAddPCurve(edge,lateral_faces.front(),false,tolerance);
                    fix.FixSameParameter(edge,tolerance);
                    split.Add(edge,lateral_faces.front());
                    contact_edges.push_back({edge,support_index});
                }
                split.Build();
                if(!split.IsDone()||!BRepCheck_Analyzer(split.Shape()).IsValid())throw std::runtime_error("Contact split failed");
                std::vector<TopoDS_Face> mapped;
                for(const auto& source:lateral_faces)for(const auto& face:split.Modified(source))
                    if(face.ShapeType()==TopAbs_FACE)mapped.push_back(TopoDS::Face(face));
                if(!mapped.empty())lateral_faces=std::move(mapped);
                pipe_shape=split.Shape();
            }
            double contact_error=0.;
            for(double fraction:{.1,.3,.5,.7,.9}) {
                const auto center=item.centers->Value(u+fraction*(v-u));
                for(const auto& support:item.supports) {
                    GeomAPI_ProjectPointOnSurf contact(center,support.surface);
                    if(!contact.NbPoints())continue;
                    double distance=1.e100;
                    for(const auto& generated:pipe.Generated(circle))if(generated.ShapeType()==TopAbs_FACE) {
                        GeomAPI_ProjectPointOnSurf actual(contact.NearestPoint(),BRep_Tool::Surface(TopoDS::Face(generated)));
                        if(actual.NbPoints())distance=std::min(distance,actual.LowerDistance());
                    }
                    contact_error=std::max(contact_error,distance);
                }
            }
            if(contact_error>20.*tolerance)throw std::runtime_error("Rolling route envelope misses its support contacts");
            const auto middle=item.centers->Value(.5*(u+v));
            double source_first,source_last;
            const auto source_curve=BRep_Tool::Curve(item.source,source_first,source_last);
            GeomAPI_ProjectPointOnCurve projection(middle,source_curve,source_first,source_last);
            auto edge_point=source_curve->Value(source_first);
            if(middle.Distance(source_curve->Value(source_last))<middle.Distance(edge_point))edge_point=source_curve->Value(source_last);
            if(projection.NbPoints()&&projection.LowerDistance()<middle.Distance(edge_point))edge_point=projection.NearestPoint();
            gp_Pnt section_center;gp_Vec section_tangent;
            item.centers->D1(.5*(u+v),section_center,section_tangent);
            GeomAPI_IntCS cross_section(source_curve,new Geom_Plane(middle,gp_Dir(section_tangent)));
            double best_distance=1.e100;
            for(int k=1;cross_section.IsDone()&&k<=cross_section.NbPoints();++k)
                if(cross_section.Point(k).Distance(middle)<best_distance) {
                    edge_point=cross_section.Point(k);best_distance=edge_point.Distance(middle);
                }
            const auto inside=edge_point.Translated(gp_Vec(edge_point,middle)*.05);
            Bnd_Box bounds;BRepBndLib::Add(rail,bounds);bounds.Enlarge(4.*(radius+middle.Distance(edge_point)));
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            TopoDS_Shape cell=BRepPrimAPI_MakeBox(gp_Pnt(x0,y0,z0),gp_Pnt(x1,y1,z1)).Shape();
            cell=clip(cell,new Geom_Plane(first,gp_Dir(tangent_first)),middle,tolerance);
            cell=clip(cell,new Geom_Plane(last,gp_Dir(tangent_last)),middle,tolerance);
            for(const auto& support:item.supports) {
                cell=clip(cell,support.surface,inside,tolerance);
            }
            {
                BRepFeat_SplitShape split(cell);std::size_t count=0;
                for(const auto& [edge,index]:contact_edges) {
                    BRepAdaptor_Curve curve(edge);const auto point=curve.Value(.5*(curve.FirstParameter()+curve.LastParameter()));
                    for(TopExp_Explorer faces(cell,TopAbs_FACE);faces.More();faces.Next()) {
                        const auto face=TopoDS::Face(faces.Current());
                        Support other;other.face=face;
                        if(!same(item.supports[index],other,tolerance))continue;
                        BRepClass_FaceClassifier classifier(face,point,tolerance);
                        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)continue;
                        ShapeFix_Edge fix;fix.FixAddPCurve(edge,face,false,tolerance);
                        split.Add(edge,face);++count;
                    }
                }
                if(count) {
                    split.Build();
                    if(!split.IsDone()||!BRepCheck_Analyzer(split.Shape()).IsValid())throw std::runtime_error("Cell contact split failed");
                    cell=split.Shape();
                }
            }
            BRepAlgoAPI_Cut cut;cut.SetArguments({cell});cut.SetTools({pipe_shape});
            cut.SetNonDestructive(true);cut.SetFuzzyValue(tolerance);cut.Build();
            if(!cut.IsDone()||!BRepCheck_Analyzer(cut.Shape()).IsValid())
                throw std::runtime_error("Rolling route cutter failed");
            Result component;component.solid=cut.Shape();
            {
                TopTools_IndexedMapOfShape selected;
                // Trim regular sections from the exact contact boundaries.
                // Boolean intersection at tangent surfaces can leave two
                // nearby seams and a flap even when each face is valid. Keep
                // the established limiting construction at a collapsed end.
                const bool exact_boundaries=binormal ||
                    (normal(item.supports[0].face,first).Dot(normal(item.supports[1].face,first))<1.-1.e-12&&
                     normal(item.supports[0].face,last).Dot(normal(item.supports[1].face,last))<1.-1.e-12);
                std::array<std::array<TopoDS_Edge,2>,2> cap_edges;
                std::array<std::array<gp_Pnt,2>,2> cap_contacts;
                if(exact_boundaries) {
                    const auto between=[&](occ::handle<Geom_Curve> curve,const gp_Pnt& a,const gp_Pnt& b) {
                        if(curve->IsClosed()&&!curve->IsPeriodic())
                            if(const auto spline=occ::down_cast<Geom_BSplineCurve>(curve->Copy())) {
                                spline->SetPeriodic();curve=spline;
                            }
                        GeomAPI_ProjectPointOnCurve pa(a,curve),pb(b,curve);
                        if(!pa.NbPoints()||!pb.NbPoints()||pa.LowerDistance()>10.*tolerance||pb.LowerDistance()>10.*tolerance)
                            throw std::runtime_error("Contact boundary misses section");
                        double u=pa.LowerDistanceParameter(),v=pb.LowerDistanceParameter();
                        if(curve->IsPeriodic())v+=curve->Period()*std::round((u-v)/curve->Period());
                        auto edge=BRepBuilderAPI_MakeEdge(curve,std::min(u,v),std::max(u,v)).Edge();
                        if(v<u)edge.Reverse();
                        // Keep the measured projection residual on each end
                        // vertex so wire assembly can join these exact curves.
                        // The projection was already bounded by 10*tolerance.
                        TopoDS_Vertex start,end;TopExp::Vertices(edge,start,end,true);
                        BRep_Builder builder;
                        builder.UpdateVertex(start,std::max(tolerance,2.*BRep_Tool::Pnt(start).Distance(a)));
                        builder.UpdateVertex(end,std::max(tolerance,2.*BRep_Tool::Pnt(end).Distance(b)));
                        return edge;
                    };
                    const auto corner_first=section_corner(item,first,gp_Dir(tangent_first),edge_point,tolerance);
                    const auto corner_last=section_corner(item,last,gp_Dir(tangent_last),edge_point,tolerance);
                    TopoDS_Edge source_edge;
                    try {source_edge=between(source_curve,corner_first,corner_last);}
                    catch(const std::runtime_error&) {
                        // A source Boolean edge can be less accurate than the
                        // analytic supports. Reconstruct their intersection
                        // without enlarging the construction tolerance.
                        GeomAPI_IntSS intersection(item.supports[0].surface,item.supports[1].surface,1.e-10);
                        for(int k=1;intersection.IsDone()&&k<=intersection.NbLines();++k) {
                            try {source_edge=between(intersection.Line(k),corner_first,corner_last);break;}catch(const std::runtime_error&){}
                        }
                        if(source_edge.IsNull())throw;
                    }
                    for(std::size_t index=0;index<2;++index) {
                        const auto& support=item.supports[index];const auto& contact=contact_edges[index].first;
                        double ca,cb;const auto cc=BRep_Tool::Curve(contact,ca,cb);
                        auto a=cc->Value(ca),b=cc->Value(cb);
                        if(a.Distance(first)>b.Distance(first))std::swap(a,b);
                        const auto boundary=[&](const gp_Pnt& center,const gp_Vec& tangent,const gp_Pnt& from,const gp_Pnt& to) {
                            if(from.Distance(to)<=tolerance)return TopoDS_Edge{};
                            GeomAPI_IntSS intersection(support.surface,new Geom_Plane(center,gp_Dir(tangent)),tolerance);
                            for(int k=1;intersection.IsDone()&&k<=intersection.NbLines();++k) {
                                try{return between(intersection.Line(k),from,to);}catch(const std::runtime_error&){}
                            }
                            throw std::runtime_error("Contact section has no boundary");
                        };
                        BRepBuilderAPI_MakeWire wire;wire.Add(source_edge);
                        const auto last_edge=boundary(last,tangent_last,corner_last,b);
                        if(!last_edge.IsNull())wire.Add(last_edge);
                        wire.Add(contact);
                        const auto first_edge=boundary(first,tangent_first,a,corner_first);
                        if(!first_edge.IsNull())wire.Add(first_edge);
                        cap_edges[0][index]=first_edge;cap_edges[1][index]=last_edge;
                        cap_contacts[0][index]=a;cap_contacts[1][index]=b;
                        if(!wire.IsDone())throw std::runtime_error("Contact boundary is not connected");
                        BRepBuilderAPI_MakeFace patch(support.surface,wire.Wire(),true);
                        if(!patch.IsDone())throw std::runtime_error("Contact support patch failed");
                        for(TopExp_Explorer e(patch.Face(),TopAbs_EDGE);e.More();e.Next()) {
                            ShapeFix_Edge fix;fix.FixAddPCurve(TopoDS::Edge(e.Current()),patch.Face(),false,tolerance);
                        }
                        ShapeFix_Face fix(patch.Face());fix.SetPrecision(tolerance);fix.SetMaxTolerance(10.*tolerance);fix.Perform();
                        const auto fixed=fix.Face();
                        // Re-evaluate the inside after periodic pcurves have
                        // been aligned; doing it before that can invert a patch.
                        BRepBuilderAPI_MakeFace oriented(support.surface,BRepTools::OuterWire(fixed),true);
                        selected.Add(oriented.Face());
                    }
                }
                const auto select_at=[&](const TopoDS_Shape& shape,const gp_Pnt& point,const auto& accepts) {
                    for(TopExp_Explorer entry(shape,TopAbs_FACE);entry.More();entry.Next()) {
                        const auto face=TopoDS::Face(entry.Current());if(!accepts(face))continue;
                        const auto surface=BRep_Tool::Surface(face);
                        GeomAPI_ProjectPointOnSurf projection(point,surface);
                        if(!projection.NbPoints()||projection.LowerDistance()>10.*tolerance)continue;
                        double u,v;projection.LowerDistanceParameters(u,v);
                        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                        if(surface->IsUPeriodic())u+=surface->UPeriod()*std::round((.5*(u0+u1)-u)/surface->UPeriod());
                        if(surface->IsVPeriodic())v+=surface->VPeriod()*std::round((.5*(v0+v1)-v)/surface->VPeriod());
                        BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),10.*tolerance);
                        if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)selected.Add(face);
                    }
                };
                if(!exact_boundaries)for(const auto& surface:item.supports) {
                    GeomAPI_ProjectPointOnSurf projection(inside,surface.surface);
                    select_at(cut.Shape(),projection.NearestPoint(),[&](const TopoDS_Face& face){Support other;other.face=face;return same(surface,other,tolerance);});
                }
                const int support_count=selected.Extent();
                const auto rim=middle.Translated(gp_Vec(edge_point,middle).Normalized()*(-radius));
                select_at(exact_boundaries?pipe_shape:cut.Shape(),rim,[&](const TopoDS_Face& face) {
                    GeomAPI_ProjectPointOnSurf projection(rim,BRep_Tool::Surface(face));
                    return projection.NbPoints()&&projection.LowerDistance()<10.*tolerance;
                });
                std::vector<TopoDS_Shape> envelope;
                for(int k=support_count+1;k<=selected.Extent();++k)envelope.push_back(selected.FindKey(k));
                if(exact_boundaries)for(std::size_t end=0;end<2;++end) {
                    const auto& a=cap_contacts[end][0];const auto& b=cap_contacts[end][1];
                    if(a.Distance(b)<=tolerance)continue;
                    const auto center=end?last:first;
                    const auto mid=center.Translated((gp_Vec(center,a)+gp_Vec(center,b)).Normalized()*radius);
                    BRepBuilderAPI_MakeWire wire;
                    if(!cap_edges[end][0].IsNull())wire.Add(cap_edges[end][0]);
                    wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(a,mid,b).Value()).Edge());
                    if(!cap_edges[end][1].IsNull())wire.Add(cap_edges[end][1]);
                    if(!wire.IsDone())throw std::runtime_error("Contact cap boundary is not connected");
                    BRepBuilderAPI_MakeFace cap(wire.Wire(),true);
                    if(!cap.IsDone())throw std::runtime_error("Contact cap failed");
                    selected.Add(cap.Face());
                }
                if(!exact_boundaries)
                for(const auto& end:std::array{std::pair{first,tangent_first},std::pair{last,tangent_last}}) {
                    if(normal(item.supports[0].face,end.first).Dot(normal(item.supports[1].face,end.first))>1.-1.e-12)continue;
                    auto corner=section_corner(item,end.first,gp_Dir(end.second),edge_point,tolerance);
                    const auto cap_inside=corner.Translated(gp_Vec(corner,end.first)*.05);
                    select_at(cut.Shape(),cap_inside,[&](const TopoDS_Face& face) {
                        const BRepAdaptor_Surface surface(face);
                        return surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(gp_Dir(end.second),1.e-7)&&surface.Plane().Distance(end.first)<10.*tolerance;
                    });
                }
                BRepBuilderAPI_Sewing sewing(10.*tolerance);
                for(int k=1;k<=selected.Extent();++k)sewing.Add(selected.FindKey(k));
                sewing.Perform();
                if(sewing.SewedShape().IsNull()||sewing.NbFreeEdges()||sewing.NbMultipleEdges()||sewing.SewedShape().ShapeType()!=TopAbs_SHELL)throw std::runtime_error("Corner shell is not closed");
                auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
                if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid()) {
                    throw std::runtime_error("Corner shell is not a solid");
                }
                component.solid=solid;
                for(const auto& source:envelope) {
                    TopoDS_Shape mapped=source;
                    if(sewing.IsModifiedSubShape(source))mapped=sewing.ModifiedSubShape(source);
                    if(mapped.ShapeType()==TopAbs_FACE)component.faces.push_back(TopoDS::Face(mapped));
                }
            }
            if(component.faces.empty())throw std::runtime_error("Rolling route lost its contact surface");
            if(result.solid.IsNull()){result=std::move(component);continue;}
            BRepAlgoAPI_Fuse fuse;fuse.SetArguments({result.solid});fuse.SetTools({component.solid});
            fuse.SetNonDestructive(true);fuse.SetFuzzyValue(tolerance);fuse.Build();
            if(!fuse.IsDone()||!BRepCheck_Analyzer(fuse.Shape()).IsValid())
                throw std::runtime_error("Rolling route join failed");
            result.faces.insert(result.faces.end(),component.faces.begin(),component.faces.end());
            TopTools_IndexedMapOfShape final_faces;TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,final_faces);
            std::vector<TopoDS_Face> mapped;
            for(const auto& face:result.faces) {
                if(final_faces.Contains(face))mapped.push_back(face);
                for(const auto& changed:fuse.Modified(face))
                    if(changed.ShapeType()==TopAbs_FACE)mapped.push_back(TopoDS::Face(changed));
            }
            result.solid=fuse.Shape();result.faces=std::move(mapped);
        }
    }
    return result;
}
}
