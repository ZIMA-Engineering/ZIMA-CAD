#pragma once
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAPI_IntCS.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <unordered_map>
#include <Geom_Line.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <BRepFill.hxx>
#include <BRepFeat_SplitShape.hxx>

namespace zima::kernel::support_driven_chamfer {
// Develop tangent plane/cylinder strips into metric coordinates. Offsetting
// their common source trace preserves authored surface distances across seams.
// Ruled patches join the exact contacts; original support bands close the tool.
inline gp_Dir normal(const TopoDS_Face& face,const gp_Pnt& point) {
    BRepAdaptor_Surface surface(face);gp_Dir result;
    if(surface.GetType()==GeomAbs_Plane) {
        result=surface.Plane().Axis().Direction();
        if(!surface.Plane().Position().Direct())result.Reverse();
    } else {
        const auto cylinder=surface.Cylinder();const gp_Vec axis(cylinder.Axis().Direction());
        auto radial=gp_Vec(cylinder.Location(),point);radial-=axis*radial.Dot(axis);result=gp_Dir(radial);
        if(!cylinder.Position().Direct())result.Reverse();
    }
    if(face.Orientation()==TopAbs_REVERSED)result.Reverse();
    return result;
}
inline bool same_support(const TopoDS_Face& first,const TopoDS_Face& second,
        const gp_Pnt& contact,double tolerance) {
    if(first.IsSame(second))return true;
    const BRepAdaptor_Surface a(first),b(second);
    if(a.GetType()!=b.GetType())return false;
    bool same=false;
    if(a.GetType()==GeomAbs_Plane)
        same=a.Plane().Axis().Direction().IsParallel(b.Plane().Axis().Direction(),1.e-9)&&a.Plane().Distance(b.Plane().Location())<tolerance;
    else if(a.GetType()==GeomAbs_Cylinder)
        same=a.Cylinder().Axis().Direction().IsParallel(b.Cylinder().Axis().Direction(),1.e-9)&&
            std::abs(a.Cylinder().Radius()-b.Cylinder().Radius())<tolerance&&gp_Lin(a.Cylinder().Axis()).Distance(b.Cylinder().Location())<tolerance;
    return same&&normal(first,contact).Dot(normal(second,contact))>1.-1.e-9;
}
struct Result {
    TopoDS_Shape solid;
    std::vector<TopoDS_Face> faces;
    std::vector<std::pair<gp_Pnt,gp_Vec>> material_probes;
};
inline gp_Pnt2d parameters(const TopoDS_Face& face,const gp_Pnt& point,double tolerance=1.e-7) {
    // An analytically recognized support can still have a different native
    // parameterization (for example an extruded circle). Use its actual UVs.
    GeomAPI_ProjectPointOnSurf projection(point,BRep_Tool::Surface(face));
    if(projection.NbPoints()==0||projection.LowerDistance()>tolerance)
        throw std::runtime_error("Contact left its support");
    double u{},v{};projection.LowerDistanceParameters(u,v);
    return {u,v};
}
struct Chart {
    TopoDS_Face face;
    gp_Pnt origin;
    gp_Dir axis,x,y;
    double radius{},sign{1.},reference_angle{},cosine{1.},sine{};
    gp_Vec2d translation;
    gp_Pnt2d raw(const gp_Pnt& point) const {
        const gp_Vec d(origin,point);
        if(radius==0.)return {d.Dot(gp_Vec(x)),d.Dot(gp_Vec(y))};
        double u=std::atan2(d.Dot(gp_Vec(y)),d.Dot(gp_Vec(x)));
        u+=2.*std::numbers::pi*std::round((reference_angle-u)/(2.*std::numbers::pi));
        return {sign*radius*u,d.Dot(gp_Vec(axis))};
    }
    gp_Pnt2d map(const gp_Pnt& point) const {
        const auto q=raw(point);
        return {cosine*q.X()-sine*q.Y()+translation.X(),sine*q.X()+cosine*q.Y()+translation.Y()};
    }
    gp_Vec2d tangent(const gp_Pnt& point,const gp_Vec& derivative) const {
        gp_Vec u(x),v(y);
        if(radius>0.) {
            auto radial=gp_Vec(origin,point);radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));radial.Normalize();
            u=gp_Vec(axis).Crossed(radial)*sign;v=gp_Vec(axis);
        }
        const double a=derivative.Dot(u),b=derivative.Dot(v);
        return {cosine*a-sine*b,sine*a+cosine*b};
    }
    gp_Pnt lift(const gp_Pnt2d& point) const {
        const double a=point.X()-translation.X(),b=point.Y()-translation.Y();
        const double u=cosine*a+sine*b,v=-sine*a+cosine*b;
        if(radius==0.)return origin.Translated(gp_Vec(x)*u+gp_Vec(y)*v);
        const double angle=u/(sign*radius);
        return origin.Translated(gp_Vec(x)*(radius*std::cos(angle))+gp_Vec(y)*(radius*std::sin(angle))+gp_Vec(axis)*v);
    }
    gp_Vec lift_tangent(const gp_Pnt& point,const gp_Vec2d& derivative) const {
        const double a=cosine*derivative.X()+sine*derivative.Y();
        const double b=-sine*derivative.X()+cosine*derivative.Y();
        if(radius==0.)return gp_Vec(x)*a+gp_Vec(y)*b;
        auto radial=gp_Vec(origin,point);radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));radial.Normalize();
        return gp_Vec(axis).Crossed(radial)*(sign*a)+gp_Vec(axis)*b;
    }
};
inline Chart chart(const TopoDS_Face& face,const gp_Pnt& middle) {
    Chart result;result.face=face;
    BRepAdaptor_Surface surface(face);
    if(surface.GetType()==GeomAbs_Plane) {
        auto plane=surface.Plane();result.origin=plane.Location();result.x=plane.Position().XDirection();
        result.axis=normal(face,middle);
        result.y=result.axis.Crossed(result.x);
    } else if(surface.GetType()==GeomAbs_Cylinder) {
        auto cylinder=surface.Cylinder();result.origin=cylinder.Location();result.axis=cylinder.Axis().Direction();
        result.x=cylinder.Position().XDirection();result.y=result.axis.Crossed(result.x);result.radius=cylinder.Radius();
        const gp_Vec d(result.origin,middle);
        result.reference_angle=std::atan2(d.Dot(gp_Vec(result.y)),d.Dot(gp_Vec(result.x)));
        auto radial=d-gp_Vec(result.axis)*d.Dot(gp_Vec(result.axis));
        result.sign=radial.Dot(gp_Vec(normal(face,middle)))>0.?1.:-1.;
    } else throw std::runtime_error("Atlas requires plane/cylinder supports");
    return result;
}
struct Sample {gp_Pnt source;std::array<gp_Pnt,2> contacts;std::array<gp_Pnt2d,2> flat;std::array<std::size_t,2> charts;};
inline Result build(const std::vector<TopoDS_Edge>& ordered,
        std::vector<std::array<TopoDS_Face,2>> faces,double first,double second,std::optional<double> angle={}) {
    for(std::size_t i=1;i<faces.size();++i) {
        TopoDS_Vertex start,end;TopExp::Vertices(ordered[i],start,end,true);
        const auto p=BRep_Tool::Pnt(start);
        if(same_support(faces[i][1],faces[i-1][0],p,1.e-7)||same_support(faces[i][0],faces[i-1][1],p,1.e-7))
            std::swap(faces[i][0],faces[i][1]);
    }
    std::array<std::vector<Chart>,2> charts;

    std::vector<std::pair<TopoDS_Edge,TopoDS_Face>> contacts_on_faces;
    std::vector<TopoDS_Face> treatment_faces;
    std::vector<std::pair<gp_Pnt,gp_Vec>> material_probes;
    std::vector<TopoDS_Vertex> vertices;
    const auto vertex=[&](const gp_Pnt& p){for(const auto& v:vertices)if(BRep_Tool::Pnt(v).Distance(p)<5.e-7)return v;auto v=BRepBuilderAPI_MakeVertex(p).Vertex();vertices.push_back(v);return v;};
    const auto at=[&](std::size_t index,double fraction) {
        BRepAdaptor_Curve curve(ordered[index]);
        const bool reverse=ordered[index].Orientation()==TopAbs_REVERSED;
        gp_Pnt p;gp_Vec d;
        curve.D1(curve.FirstParameter()+(reverse?1.-fraction:fraction)*(curve.LastParameter()-curve.FirstParameter()),p,d);
        if(reverse)d.Reverse();
        return std::pair{p,d};
    };
    // Keep the preceding and following turns of the closed strip. An offset
    // near the authored seam may cross it; modulo-indexing one development
    // would incorrectly jump by the strip's accumulated rigid displacement.
    for(std::size_t side=0;side<2;++side)for(std::size_t step=0;step<3*ordered.size();++step) {
        const auto i=step%ordered.size();
        auto current=chart(faces[i][side],at(i,.5).first);
        if(step) {
            const auto [point,direction]=at(i,0.);
            const auto old_t=charts[side].back().tangent(point,direction).Normalized();
            const auto new_t=current.tangent(point,direction).Normalized();
            current.cosine=new_t.Dot(old_t);current.sine=new_t.Crossed(old_t);
            const auto old_p=charts[side].back().map(point),new_p=current.map(point);
            current.translation=gp_Vec2d(new_p,old_p);
            const auto n0=normal(faces[(i+ordered.size()-1)%ordered.size()][side],point);
            const auto n1=normal(faces[i][side],point);
            if(n0.Dot(n1)<1.-1.e-7)throw std::runtime_error("Atlas seam is not tangent");
        }
        charts[side].push_back(current);
    }
    for(std::size_t i=0;i<ordered.size();++i) {
        std::array<double,2> signs;
        gp_Vec material_direction;
        for(std::size_t side=0;side<2;++side) {
            const auto [p,d]=at(i,.5);const auto& chart=charts[side][ordered.size()+i];
            auto t=chart.tangent(p,d).Normalized();gp_Vec2d left(-t.Y(),t.X());
            const auto q=chart.map(p);
            bool found=false;
            for(double sign:{1.,-1.}) {
                const auto test=chart.lift(q.Translated(left*(.001*sign)));
                if(BRepClass_FaceClassifier(chart.face,test,1.e-7).State()==TopAbs_IN) {
                    signs[side]=sign;found=true;
                    material_direction+=chart.lift_tangent(p,left*sign);
                    break;
                }
            }
            if(!found)throw std::runtime_error("Atlas could not classify a support side");
        }
        if(material_direction.Magnitude()<1.e-10)throw std::runtime_error("Degenerate material wedge");
        material_probes.emplace_back(at(i,.5).first,material_direction.Normalized());
        std::unordered_map<double,Sample> samples;
        const auto evaluate=[&](double fraction) {
            if(const auto found=samples.find(fraction);found!=samples.end())return found->second;
            const auto [point,direction]=at(i,fraction);Sample value;value.source=point;
            for(std::size_t side=0;side<2;++side) {
                const auto& own=charts[side][ordered.size()+i];
                const auto tangent=own.tangent(point,direction).Normalized();
                const auto target=own.map(point).Translated(gp_Vec2d(-tangent.Y(),tangent.X())*((side?second:first)*signs[side]));
                value.flat[side]=target;
                bool found=false;
                for(std::size_t shift=0;shift<charts[side].size();++shift) {
                    const auto k=(ordered.size()+i+shift)%charts[side].size();const auto candidate=charts[side][k].lift(target);
                    // A development can overlap itself after several turns.
                    // The contact's chord cannot exceed its authored intrinsic
                    // distance; reject remote layers of the same flat chart.
                    if(candidate.Distance(point)>(side?second:first)+1.e-6)continue;
                    if(charts[side][k].map(candidate).Distance(target)>1.e-6)continue;
                    const auto uv=parameters(charts[side][k].face,candidate);
                    const auto uv_state=BRepClass_FaceClassifier(charts[side][k].face,uv,1.e-7).State();
                    if(uv_state!=TopAbs_IN&&uv_state!=TopAbs_ON)continue;
                    value.contacts[side]=candidate;value.charts[side]=k;found=true;break;
                }
                if(!found) {
                    throw std::runtime_error("Atlas offset left its support strip");
                }
            }
            if(angle) {
                const auto& first_chart=charts[0][value.charts[0]];
                const auto a=value.contacts[0];
                const auto oriented_normal=normal(first_chart.face,a);
                const auto& own=charts[0][ordered.size()+i];
                // Parallel curves in a developed support have parallel
                // tangents. Their speed changes, but their direction does
                // not require differentiating the source approximation twice.
                auto tangent=first_chart.lift_tangent(a,own.tangent(point,direction));
                tangent-=gp_Vec(oriented_normal)*tangent.Dot(gp_Vec(oriented_normal));tangent.Normalize();
                auto inward=gp_Vec(oriented_normal).Crossed(tangent);
                if(inward.Dot(gp_Vec(point,a))<0.)inward.Reverse();
                const double normal_sign=gp_Vec(point,value.contacts[1]).Dot(gp_Vec(oriented_normal))>0.?1.:-1.;
                const gp_Dir ray(-std::cos(*angle)*inward+normal_sign*std::sin(*angle)*gp_Vec(oriented_normal));
                double nearest=1.e100,flat_gap=1.e100;std::optional<std::size_t> chosen;
                gp_Pnt contact;gp_Pnt2d flat;
                for(std::size_t k=0;k<charts[1].size();++k) {
                    const auto& chart=charts[1][k];
                    GeomAPI_IntCS intersection(new Geom_Line(a,ray),BRep_Tool::Surface(chart.face));
                    if(!intersection.IsDone())continue;
                    for(int n=1;n<=intersection.NbPoints();++n) {
                        double u,v,w;intersection.Parameters(n,u,v,w);if(w<=1.e-7)continue;
                        const auto state=BRepClass_FaceClassifier(chart.face,gp_Pnt2d(u,v),1.e-7).State();
                        if(state!=TopAbs_IN&&state!=TopAbs_ON)continue;
                        const auto p=intersection.Point(n);
                        const auto q=chart.map(p);
                        const double gap=q.Distance(value.flat[1]);
                        if(w<nearest-1.e-7||(std::abs(w-nearest)<1.e-7&&gap<flat_gap)) {
                            nearest=w;flat_gap=gap;chosen=k;contact=p;flat=q;
                        }
                    }
                }
                if(!chosen)throw std::runtime_error("Chamfer angle ray misses its opposite support strip");
                value.contacts[1]=contact;value.charts[1]=*chosen;value.flat[1]=flat;
                const double measured=std::asin(std::abs(gp_Vec(a,contact).Normalized().Dot(gp_Vec(oriented_normal))));
                if(std::abs(measured-*angle)>1.e-9)throw std::runtime_error("Chamfer angle changed along the route");
            }
            samples.emplace(fraction,value);
            return value;
        };
        std::vector<double> cuts{0.,1.};
        auto previous=evaluate(0.);
        for(int sample=1;sample<=128;++sample) {
            const double fraction=sample/128.;const auto current=evaluate(fraction);
            for(std::size_t side=0;side<2;++side) {
                const auto& before=charts[side][previous.charts[side]].face;
                const auto& after=charts[side][current.charts[side]].face;
                if(before.IsSame(after))continue;
                double low=(sample-1)/128.,high=fraction;
                for(int iteration=0;iteration<40;++iteration) {
                    const double mid=.5*(low+high);const auto test=evaluate(mid);
                    if(charts[side][test.charts[side]].face.IsSame(before))low=mid;else high=mid;
                }
                // Classification tolerances locate the transition, but the
                // contact vertex must lie on the actual shared trim edge.
                NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> old_edges;TopExp::MapShapes(before,TopAbs_EDGE,old_edges);
                double nearest=1.e100;std::optional<std::pair<gp_Pnt2d,gp_Vec2d>> seam;
                const auto& old_chart=charts[side][previous.charts[side]];
                const auto guess=evaluate(.5*(low+high)).flat[side];
                for(TopExp_Explorer e(after,TopAbs_EDGE);e.More();e.Next())if(old_edges.Contains(e.Current())) {
                    const BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
                    const auto a=old_chart.map(curve.Value(curve.FirstParameter()));
                    const auto b=old_chart.map(curve.Value(curve.LastParameter()));
                    const auto m=old_chart.map(curve.Value(.5*(curve.FirstParameter()+curve.LastParameter())));
                    gp_Vec2d direction(a,b);if(direction.Magnitude()<1.e-9)continue;direction.Normalize();
                    if(std::abs(direction.Crossed(gp_Vec2d(a,m)))>1.e-7)continue;
                    const double gap=std::abs(direction.Crossed(gp_Vec2d(a,guess)));
                    if(gap<nearest){nearest=gap;seam=std::pair{a,direction};}
                }
                if(seam&&nearest<1.e-4) {
                    low=(sample-1)/128.;high=fraction;
                    const auto distance=[&](double t){return seam->second.Crossed(gp_Vec2d(seam->first,evaluate(t).flat[side]));};
                    const double first_distance=distance(low);
                    if(first_distance*distance(high)<=0.)for(int iteration=0;iteration<45;++iteration) {
                        const double mid=.5*(low+high);
                        if(first_distance*distance(mid)>0.)low=mid;else high=mid;
                    }
                }
                cuts.push_back(.5*(low+high));
            }
            previous=current;
        }
        std::ranges::sort(cuts);
        cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<1.e-9;}),cuts.end());
        for(std::size_t part=1;part<cuts.size();++part) {
            const double start=cuts[part-1],end=cuts[part];if(end-start<1.e-9)continue;
            const auto middle=evaluate(.5*(start+end));
            std::array<TopoDS_Edge,2> contact;
            for(std::size_t side=0;side<2;++side) {
                const auto face=charts[side][middle.charts[side]].face;
                const auto surface=BRep_Tool::Surface(face);
                const auto center=parameters(face,middle.contacts[side]);
                const auto uv=[&](double fraction) {
                    const auto p=evaluate(start+fraction*(end-start)).contacts[side];
                    const auto q=parameters(face,p,1.e-6);double u=q.X(),v=q.Y();
                    if(surface->IsUPeriodic())u+=surface->UPeriod()*std::round((center.X()-u)/surface->UPeriod());
                    return gp_Pnt2d(u,v);
                };
                occ::handle<Geom2d_BSplineCurve> fitted;
                for(int count=16;count<=4096;count*=2) {
                    occ::handle<NCollection_HArray1<gp_Pnt2d>> points=new NCollection_HArray1<gp_Pnt2d>(1,count+1);
                    occ::handle<NCollection_HArray1<double>> parameters=new NCollection_HArray1<double>(1,count+1);
                    for(int n=0;n<=count;++n){points->SetValue(n+1,uv(double(n)/count));parameters->SetValue(n+1,double(n)/count);}
                    Geom2dAPI_Interpolate interpolation(points,parameters,false,1.e-10);interpolation.Perform();
                    if(!interpolation.IsDone())throw std::runtime_error("Contact interpolation failed");
                    double error=0.;
                    for(int n=0;n<count;++n) {
                        const double t=(n+.5)/count;const auto q=interpolation.Curve()->Value(t);
                        const auto expected=evaluate(start+t*(end-start)).contacts[side];
                        error=std::max(error,surface->Value(q.X(),q.Y()).Distance(expected));
                    }

                    if(error<1.e-7){fitted=interpolation.Curve();break;}
                }
                if(fitted.IsNull())throw std::runtime_error("Contact approximation exceeded tolerance");
                const auto a=evaluate(start).contacts[side],b=evaluate(end).contacts[side];
                BRepBuilderAPI_MakeEdge edge(fitted,surface,vertex(a),vertex(b),0.,1.);
                if(!edge.IsDone())throw std::runtime_error("Contact edge failed");
                contact[side]=edge.Edge();
                if(!BRepLib::BuildCurve3d(contact[side],1.e-7))throw std::runtime_error("Contact 3D curve failed");
                contacts_on_faces.push_back({contact[side],face});
            }
            const auto treatment=BRepFill::Face(contact[0],contact[1]);
            if(treatment.IsNull())throw std::runtime_error("Ruled treatment face failed");
            treatment_faces.push_back(treatment);
        }

    }
    BRep_Builder band_builder;TopoDS_Compound supports;band_builder.MakeCompound(supports);
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> unique_supports;
    for(const auto& pair:faces)for(const auto& face:pair)unique_supports.Add(face);
    for(const auto& face:unique_supports)band_builder.Add(supports,face);
    BRepFeat_SplitShape split(supports);
    for(const auto& [edge,face]:contacts_on_faces)split.Add(edge,face);
    split.Build();
    if(!split.IsDone())throw std::runtime_error("Support contact split failed");
    std::vector<TopoDS_Face> bands;
    for(TopExp_Explorer item(split.Shape(),TopAbs_FACE);item.More();item.Next()) {
        const auto face=TopoDS::Face(item.Current());bool band=false;
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())
            for(const auto& source:ordered)if(edge.Current().IsSame(source))band=true;
        if(band)bands.push_back(face);
    }
    BRepBuilderAPI_Sewing sewing(1.e-6);
    for(const auto& face:bands)sewing.Add(face);
    for(const auto& face:treatment_faces)sewing.Add(face);
    sewing.Perform();
    if(sewing.NbFreeEdges()||sewing.NbMultipleEdges()||sewing.SewedShape().ShapeType()!=TopAbs_SHELL)throw std::runtime_error("Chamfer band is not closed");
    auto tool=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
    if(!BRepLib::OrientClosedSolid(tool)||!BRepCheck_Analyzer(tool).IsValid())throw std::runtime_error("Chamfer band is not a solid");
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> tool_faces;TopExp::MapShapes(tool,TopAbs_FACE,tool_faces);
    for(auto& face:treatment_faces) {
        if(sewing.IsModified(face))face=TopoDS::Face(sewing.Modified(face));
        if(!tool_faces.Contains(face))throw std::runtime_error("Sewn chamfer lost a treatment face");
        face=TopoDS::Face(tool_faces.FindKey(tool_faces.FindIndex(face)));
    }
    return {tool,std::move(treatment_faces),std::move(material_probes)};
}
}
