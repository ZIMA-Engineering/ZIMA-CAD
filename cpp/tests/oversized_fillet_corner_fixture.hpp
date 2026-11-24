#pragma once
#include <GC_MakeArcOfCircle.hxx>

// Included by the native Fillet suite after its ordinary assertion helpers.
void verify_oversized_fillet_corners() {
    using namespace zima::kernel;
    for(const double degrees:{60.,90.,120.}) {
        constexpr double small=4.,length=60.;
        const double alpha=degrees*std::numbers::pi/180.,d=small/std::tan(alpha/2.);
        const gp_Pnt a(0,d,0),b(d*std::sin(alpha),d*std::cos(alpha),0),center(small,d,0);
        const gp_Pnt p0(0,length,0),p3(length*std::sin(alpha),length*std::cos(alpha),0),p4(p3.X(),p0.Y()+p3.Y(),0);
        const auto mid=center.Translated(-gp_Vec(gp_Pnt(0,0,0),center).Normalized()*small);
        BRepBuilderAPI_MakeWire wire;
        wire.Add(BRepBuilderAPI_MakeEdge(p0,a));wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(a,mid,b).Value()));
        wire.Add(BRepBuilderAPI_MakeEdge(b,p3));wire.Add(BRepBuilderAPI_MakeEdge(p3,p4));wire.Add(BRepBuilderAPI_MakeEdge(p4,p0));
        const auto base=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Vec(0,0,100)).Shape();
        for(const bool rotated:{false,true}) {
            gp_Trsf placement;
            if(rotated)placement.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.731);
            const auto input=BRepBuilderAPI_Transform(base,placement,true).Shape();
            const auto inverse=placement.Inverted();
            std::array<TopoDS_Edge,3> route;
            for(TopExp_Explorer item(input,TopAbs_EDGE);item.More();item.Next()) {
                const auto edge=TopoDS::Edge(item.Current());const BRepAdaptor_Curve c(edge);
                const auto start=c.Value(c.FirstParameter()).Transformed(inverse),end=c.Value(c.LastParameter()).Transformed(inverse);
                const auto middle=c.Value(.5*(c.FirstParameter()+c.LastParameter())).Transformed(inverse);
                if(std::abs(start.Z()-100)>1.e-6||std::abs(end.Z()-100)>1.e-6)continue;
                if(c.GetType()==GeomAbs_Circle)route[1]=edge;
                else if(std::abs(middle.X())<1.e-6)route[0]=edge;
                else if(std::abs(middle.X()*std::cos(alpha)-middle.Y()*std::sin(alpha))<1.e-6)route[2]=edge;
            }
            for(const auto& edge:route)require(!edge.IsNull(),"Oversized corner fixture lost a route edge");
            std::ostringstream before;BRepTools::Write(input,before);
            const std::array<std::size_t,1> count{3};
            for(const double radius:{6.,9.,12.}) {
                double forward_volume{};
                for(const bool reverse:{false,true}) {
                    auto selected=route;if(reverse)std::ranges::reverse(selected);
                    std::cerr<<"fillet-test: oversized angle="<<degrees<<" radius="<<radius<<" rotated="<<rotated<<" reversed="<<reverse<<'\n';
                    const auto result=occt_planar_edge_fillet(input,selected,radius,1.e-7,{},
                        OcctPlanarEdgeFilletEndMode::AutomaticBodyEnvelope,count);
                    require(result.history&&BRepCheck_Analyzer(result.shape,true,true,true).IsValid(),"Oversized corner is not exact-valid");
                    BOPAlgo_ArgumentAnalyzer check;check.SetShape1(result.shape);check.SelfInterMode()=true;check.CurveOnSurfaceMode()=true;check.Perform();
                    require(!check.HasFaulty(),"Oversized corner has intersecting or inaccurate boundaries");
                    require(result.fillet_faces.size()==3&&std::ranges::all_of(result.fillet_faces,[](const auto& faces){return !faces.empty();}),
                        "Oversized corner lost source ancestry");
                    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> neighbors;
                    TopExp::MapShapesAndAncestors(result.shape,TopAbs_EDGE,TopAbs_FACE,neighbors);
                    int patch_count{},contacts{};
                    for(TopExp_Explorer face_item(result.shape,TopAbs_FACE);face_item.More();face_item.Next()) {
                        const auto face=TopoDS::Face(face_item.Current());
                        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_BSplineSurface)continue;
                        ++patch_count;
                        for(TopExp_Explorer edge_item(face,TopAbs_EDGE);edge_item.More();edge_item.Next()) {
                            const auto edge=TopoDS::Edge(edge_item.Current());if(BRep_Tool::Degenerated(edge))continue;
                            for(const auto& other:neighbors.FindFromKey(edge))if(!other.IsSame(face)) {
                                const auto adjacent=TopoDS::Face(other);
                                require(BRepAdaptor_Surface(adjacent).GetType()==GeomAbs_Cylinder,"Oversized patch lost its cylindrical contact");
                                const double contact_radius=BRepAdaptor_Surface(adjacent).Cylinder().Radius();
                                require(std::abs(contact_radius-radius)<1.e-7||std::abs(contact_radius-small)<1.e-7,"Oversized patch changed a support radius");
                                double f,l;const auto uv=BRep_Tool::CurveOnSurface(edge,face,f,l);
                                const auto surface=BRep_Tool::Surface(face);
                                const auto cylinder=BRepAdaptor_Surface(adjacent).Cylinder();
                                for(int j=1;j<20;++j) {
                                    const auto q=uv->Value(f+(l-f)*j/20.);
                                    gp_Pnt p;gp_Vec du,dv;surface->D1(q.X(),q.Y(),p,du,dv);
                                    const auto axis=gp_Vec(cylinder.Axis().Direction());
                                    const auto offset=gp_Vec(cylinder.Location(),p);
                                    const auto radial=offset-axis*offset.Dot(axis);
                                    require(std::abs(radial.Magnitude()-contact_radius)<1.e-7,"Oversized patch contact is not position-continuous");
                                    const auto n1=du.Crossed(dv),n2=radial;
                                    require(std::min(n1.Angle(n2),n1.Angle(-n2))<1.e-6,"Oversized patch contact is not tangent");
                                }
                                ++contacts;
                            }
                        }
                    }
                    require(patch_count==1&&contacts==3,"Oversized corner did not retain all three tangent contacts");
                    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids;
                    TopExp::MapShapes(result.shape,TopAbs_SOLID,solids);require(solids.Extent()==1,"Oversized corner split the Body");
                    // Independent horizontal-section integral: straight offsets
                    // d=R*(1-sqrt(1-s*s)), remaining corner radius r*sqrt(1-s*s).
                    const double q=1./std::tan(alpha/2.)-(std::numbers::pi-alpha)/2.;
                    const double removed=2.*length*radius*radius*(1.-std::numbers::pi/4.)-
                        radius*radius*radius/std::sin(alpha)*(5./3.-std::numbers::pi/2.)-radius*small*small*q/3.;
                    near(volume(result.shape),volume(input)-removed,"Oversized corner disagrees with its independent volume integral");
                    if(reverse)near(volume(result.shape),forward_volume,"Oversized corner depends on route order");
                    else forward_volume=volume(result.shape);
                    std::ostringstream after;BRepTools::Write(input,after);require(before.str()==after.str(),"Oversized corner modified its source BREP");
                    std::stringstream archive;BRepTools::Write(result.shape,archive);TopoDS_Shape restored;BRep_Builder builder;BRepTools::Read(restored,archive,builder);
                    require(BRepCheck_Analyzer(restored,true,true,true).IsValid(),"Oversized corner did not reopen");
                    near(volume(restored),volume(result.shape),"Oversized corner changed on BREP reopen");
                    const auto hole_center=gp_Pnt(p4.X()*.5,p4.Y()*.5,-1).Transformed(placement);
                    const auto hole_axis=gp_Dir(0,0,1).Transformed(placement);
                    BRepAlgoAPI_Cut hole;hole.SetArguments({result.shape});
                    hole.SetTools({BRepPrimAPI_MakeCylinder(gp_Ax2(hole_center,hole_axis),1.,102.).Shape()});
                    hole.SetNonDestructive(true);hole.Build();
                    require(hole.IsDone()&&BRepCheck_Analyzer(hole.Shape()).IsValid(),"Dependent hole failed after oversized corner");
                    near(volume(hole.Shape()),volume(result.shape)-100.*std::numbers::pi,"Dependent hole lost material after oversized corner");
                }
            }
        }
    }
}
