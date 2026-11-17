#pragma once
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>

namespace zima::test {
// A stepped, rounded prism trimmed by a cylinder has a closed nonplanar lip.
// The exact planar profile is authored independently of the treatment builder.
inline void verify_closed_support_chamfer() {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto point=[](double x,double z){return gp_Pnt(x,-60.,z);};
    BRepBuilderAPI_MakeWire wire;
    const auto line=[&](gp_Pnt a,gp_Pnt b){wire.Add(BRepBuilderAPI_MakeEdge(a,b));};
    const auto arc=[&](gp_Pnt a,gp_Pnt m,gp_Pnt b){wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(a,m,b).Value()));};
    const double r6=6./std::sqrt(2.),r4=4./std::sqrt(2.);
    line(point(-50,0),point(50,0));line(point(50,0),point(50,20));line(point(50,20),point(18,20));
    arc(point(18,20),point(18-r6,26-r6),point(12,26));line(point(12,26),point(12,36));
    arc(point(12,36),point(8+r4,36+r4),point(8,40));line(point(8,40),point(-8,40));
    arc(point(-8,40),point(-8-r4,36+r4),point(-12,36));line(point(-12,36),point(-12,26));
    arc(point(-12,26),point(-18+r6,26-r6),point(-18,20));line(point(-18,20),point(-50,20));
    line(point(-50,20),point(-50,0));check(wire.IsDone(),"Closed chamfer profile failed");
    const auto prism=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()),gp_Vec(0,120,0)).Shape();
    BRepAlgoAPI_Common common(prism,BRepPrimAPI_MakeCylinder(40,40).Shape());common.Build();
    check(common.IsDone()&&BRepCheck_Analyzer(common.Shape()).IsValid(),"Closed chamfer input failed");
    const auto input=common.Shape();
    struct Member {TopoDS_Edge edge;TopoDS_Face cylinder,other;};
    std::vector<Member> members;
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> adjacency;TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(int i=1;i<=adjacency.Extent();++i) {
        const auto edge=TopoDS::Edge(adjacency.FindKey(i));const auto& faces=adjacency.FindFromIndex(i);
        if(faces.Size()!=2)continue;
        const BRepAdaptor_Curve c(edge);if(c.Value(.5*(c.FirstParameter()+c.LastParameter())).Z()<1.)continue;
        auto it=faces.cbegin();auto first=TopoDS::Face(*it++),second=TopoDS::Face(*it);
        if(first.IsSame(second))continue;
        const auto outer=[](const TopoDS_Face& f){const BRepAdaptor_Surface s(f);return s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-40.)<1.e-8;};
        if(outer(first)==outer(second))continue;
        if(!outer(first))std::swap(first,second);
        members.push_back({edge,first,second});
    }
    check(members.size()>8,"Closed chamfer fixture lost its mixed route");
    using Mode=kernel::OcctPlanarEdgeChamferMode;
    for(const auto mode:{Mode::EqualDistance,Mode::TwoDistances,Mode::DistanceAngle})
      for(const bool flip:{false,true}) {
        double expected_volume{};
        for(const bool reversed:{false,true}) {
            std::vector<kernel::OcctPlanarEdgeChamferSelection> selections;
            for(const auto& member:members)selections.push_back({member.edge,flip?member.other:member.cylinder});
            if(reversed) {
                std::ranges::reverse(selections);
                std::rotate(selections.begin(),selections.begin()+selections.size()/3,selections.end());
                for(auto& selection:selections)selection.edge.Reverse();
            }
            const double secondary=mode==Mode::DistanceAngle?std::numbers::pi/6.:3.;
            const auto result=kernel::occt_planar_edge_chamfer(input,selections,mode,2.,secondary,1.e-7);
            check(BRepCheck_Analyzer(result.shape).IsValid(),"Closed chamfer produced invalid topology");
            check(!result.chamfer_faces.empty()&&!result.chamfer_faces.front().empty(),"Closed chamfer lost face ownership");
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> solids;TopExp::MapShapes(result.shape,TopAbs_SOLID,solids);
            check(solids.Extent()==1,"Closed chamfer split the Body");
            GProp_GProps properties;BRepGProp::VolumeProperties(result.shape,properties,1.e-10);
            if(!reversed)expected_volume=properties.Mass();
            else check(std::abs(properties.Mass()-expected_volume)<1.e-4,"Closed chamfer depends on route start or orientation");
            // On a long horizontal lip the developed construction reduces to
            // independent cylinder-arc and plane-distance measurements.
            const double a=2.,b=mode==Mode::EqualDistance?2.:mode==Mode::TwoDistances?3.:2.*std::tan(secondary);
            const auto cylinder_contact=gp_Pnt(40.,0.,20.-(flip?b:a));
            const auto plane_contact=gp_Pnt(40.-(flip?a:b),0.,20.);
            BRep_Builder boundary_builder;TopoDS_Compound boundary;boundary_builder.MakeCompound(boundary);
            for(TopExp_Explorer edge(result.shape,TopAbs_EDGE);edge.More();edge.Next())boundary_builder.Add(boundary,edge.Current());
            for(const auto& p:{cylinder_contact,plane_contact}) {
                BRepExtrema_DistShapeShape gap(BRepBuilderAPI_MakeVertex(p).Vertex(),boundary);
                check(gap.IsDone()&&gap.Value()<2.e-6,"Closed chamfer changed A/B or angle at a planar section");
            }
            if(mode==Mode::DistanceAngle) {
                NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> joined;TopExp::MapShapesAndAncestors(result.shape,TopAbs_EDGE,TopAbs_FACE,joined);
                NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> treatment;
                for(const auto& faces:result.chamfer_faces)for(const auto& face:faces)treatment.Add(face);
                std::size_t measured{};
                for(int i=1;i<=joined.Extent();++i) {
                    const auto& faces=joined.FindFromIndex(i);if(faces.Size()!=2)continue;
                    auto it=faces.cbegin();auto first=TopoDS::Face(*it++),second=TopoDS::Face(*it);
                    if(treatment.Contains(first)==treatment.Contains(second))continue;
                    if(!treatment.Contains(first))std::swap(first,second);
                    const BRepAdaptor_Surface support(second);
                    const bool cylinder=support.GetType()==GeomAbs_Cylinder&&std::abs(support.Cylinder().Radius()-40.)<1.e-7;
                    if(cylinder==flip)continue;
                    const BRepAdaptor_Curve curve(TopoDS::Edge(joined.FindKey(i)));
                    for(double fraction:{.2,.5,.8}) {
                        const auto p=curve.Value(curve.FirstParameter()+fraction*(curve.LastParameter()-curve.FirstParameter()));
                        const auto normal=[&](const TopoDS_Face& face) {
                            GeomAPI_ProjectPointOnSurf projection(p,BRep_Tool::Surface(face));
                            check(projection.NbPoints()>0,"Chamfer contact normal projection failed");
                            double u,v;projection.LowerDistanceParameters(u,v);gp_Pnt ignored;gp_Vec du,dv;
                            BRepAdaptor_Surface(face).D1(u,v,ignored,du,dv);return gp_Dir(du.Crossed(dv));
                        };
                        const double angle=std::acos(std::clamp(std::abs(normal(first).Dot(normal(second))),0.,1.));
                        check(std::abs(angle-secondary)<2.e-4,"Closed chamfer lost its angle on a curved support");++measured;
                    }
                }
                check(measured>12,"Closed chamfer angle check omitted its curved contacts");
            }
            std::cout<<"Closed chamfer passed mode="<<static_cast<int>(mode)<<" flip="<<flip<<" reversed="<<reversed<<'\n';
        }
      }
}
} // namespace zima::test
