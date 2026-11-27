#include "../modules/kernel_occt/src/directional_projection.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Precision.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
namespace p=zima::kernel::detail;
constexpr double tolerance=1e-7;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
TopoDS_Edge line(gp_Pnt a,gp_Pnt b) {return BRepBuilderAPI_MakeEdge(a,b).Edge();}
TopoDS_Face plane(double z,double xmin=-20,double xmax=20,double ymin=-20,double ymax=20) {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,z),gp_Dir(0,0,1)),xmin,xmax,ymin,ymax).Face();
}
TopoDS_Face analytical_face(const TopoDS_Shape& shape,GeomAbs_SurfaceType type) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()==type)return face;
    }
    throw std::runtime_error("Missing analytic test surface");
}
p::DirectionalProjection project(const TopoDS_Edge& source,std::vector<p::ProjectionTarget> targets,
        gp_Pln sketch=gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),gp_Dir direction=gp_Dir(0,0,1),bool both=false) {
    auto result=p::project_directionally({{source,"authored-curve"}},targets,sketch,direction,tolerance,both);
    const BRepAdaptor_Curve input(source);
    for(const auto& piece:result.curves) {
        // A complete closed edge may choose its own native seam; every partial
        // interval must end at the independently evaluated source ray positions.
        if(input.IsClosed()&&std::abs(piece.source_first-input.FirstParameter())<1e-9&&
            std::abs(piece.source_last-input.LastParameter())<1e-9)continue;
        const BRepAdaptor_Curve output(piece.edge);
        const auto on_source_plane=[&](gp_Pnt point) {
            const double depth=gp_Vec(sketch.Location(),point).Dot(gp_Vec(sketch.Axis().Direction()))/
                direction.Dot(sketch.Axis().Direction());
            return point.Translated(-depth*gp_Vec(direction));
        };
        const auto a=on_source_plane(output.Value(output.FirstParameter()));
        const auto b=on_source_plane(output.Value(output.LastParameter()));
        const auto start=input.Value(piece.source_first),end=input.Value(piece.source_last);
        require((a.Distance(start)<tolerance*8&&b.Distance(end)<tolerance*8)||
            (a.Distance(end)<tolerance*8&&b.Distance(start)<tolerance*8),"Trimmed projection endpoints do not match their source rays");
    }
    for(const auto& piece:result.curves)for(const auto& parent:piece.target_keys) {
        const auto target=std::ranges::find(targets,parent,&p::ProjectionTarget::target_key);
        require(target!=targets.end(),"Unknown target parent");
        double first{},last{};const auto pcurve=BRep_Tool::CurveOnSurface(piece.edge,target->face,first,last);
        require(!pcurve.IsNull(),"Missing target pcurve");
        const auto surface=BRep_Tool::Surface(target->face);BRepAdaptor_Curve curve(piece.edge);
        for(int i=0;i<=8;++i) {
            const double t=curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/8.;
            const auto uv=pcurve->Value(t);
            const double error=surface->Value(uv.X(),uv.Y()).Distance(curve.Value(t));
            if(error>=tolerance*8)throw std::runtime_error("Native 3D curve and pcurve disagree on "+parent+
                " at "+std::to_string(t)+" (error "+std::to_string(error)+", range "+
                std::to_string(curve.FirstParameter())+" .. "+std::to_string(curve.LastParameter())+
                ", source "+std::to_string(piece.source_first)+" .. "+std::to_string(piece.source_last)+")");
        }
    }
    return result;
}
void check(const p::DirectionalProjection& result,const std::function<bool(const gp_Pnt&)>& equation,
        bool complete=true) {
    require(!result.ambiguous,"Unexpected ambiguous projection");
    if(complete)require(result.gaps.empty(),"Unexpected missing source interval");
    require(!result.curves.empty(),"Missing projected curves");
    for(const auto& piece:result.curves) {
        require(piece.source_key=="authored-curve","Lost source identity");
        require(!piece.target_keys.empty(),"Lost target identity");
        require(BRepCheck_Analyzer(piece.edge).IsValid(),"Invalid projected edge");
        BRepAdaptor_Curve curve(piece.edge);
        for(int i=0;i<=16;++i) {
            const auto point=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/16.);
            require(equation(point),"Independent projection equation failed");
        }
    }
}
}

int main() {
    try {
        int passed=0;
        const auto run=[&](const char* name,const auto& test){test();++passed;std::cout<<"PASS "<<name<<'\n';};
        const auto source=line({-6,2,0},{6,2,0});
        const gp_Pln source_plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
        run("analytic continuation keeps exact support and forward branch",[&] {
            const auto support=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10);
            p::ProjectionTarget cylinder{BRepBuilderAPI_MakeFace(support,tolerance).Face(),"partial-cylinder",{},true};
            const gp_Pln sketch(gp_Pnt(20,0,0),gp_Dir(1,0,0));
            const auto result=project(line({20,2,100},{20,2,110}),{cylinder},sketch,gp_Dir(-1,0,0));
            check(result,[](const auto& point){return std::abs(point.X()-std::sqrt(96.))<tolerance*8&&
                std::abs(point.Y()-2)<tolerance*8&&point.Z()>=100-tolerance&&point.Z()<=110+tolerance;});
            p::ProjectionTarget infinite_plane{BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,7),gp_Dir(0,0,1))).Face(),"partial-plane",{},true};
            check(project(source,{infinite_plane}),[](const auto& point){return std::abs(point.Z()-7)<tolerance;});
            auto other=infinite_plane;other.target_key="another-part-of-the-same-support";
            const auto coincident=project(source,{infinite_plane,other});
            check(coincident,[](const auto& point){return std::abs(point.Z()-7)<tolerance;});
            for(const auto& curve:coincident.curves)require(curve.target_keys.size()==2,"Equivalent selected supports lost an authored parent");
            require(project(source,{infinite_plane},source_plane,gp_Dir(0,0,-1)).curves.empty(),
                "Analytic continuation silently reversed the selected direction");
        });
        run("extended cone retains its nappe and sphere its nearest root",[&] {
            const gp_Pln sketch(gp_Pnt(100,0,0),gp_Dir(1,0,0));
            for(double slope:{.2,-.2}) {
                const double angle=std::atan(slope),apex=-10/std::sin(angle),sign=slope>0?1.:-1.;
                const auto support=new Geom_ConicalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),angle,10);
                p::ProjectionTarget cone{BRepBuilderAPI_MakeFace(support,0,2*std::acos(-1.),
                    slope>0?apex:-Precision::Infinite(),slope>0?Precision::Infinite():apex,tolerance).Face(),"partial-cone",{},true};
                check(project(line({100,2,sign*100},{100,2,sign*110}),{cone},sketch,gp_Dir(-1,0,0)),[&](const auto& point){
                    const double radius=10+slope*point.Z();return radius>0&&std::abs(point.X()-std::sqrt(radius*radius-4))<tolerance*20;
                });
                require(project(line({100,2,-sign*100},{100,2,-sign*110}),{cone},sketch,gp_Dir(-1,0,0)).curves.empty(),
                    "Extended cone jumped through its apex to the other nappe");
            }
            p::ProjectionTarget sphere{analytical_face(BRepPrimAPI_MakeSphere(10).Shape(),GeomAbs_Sphere),"partial-sphere",{},true};
            check(project(line({100,2,1},{100,2,3}),{sphere},sketch,gp_Dir(-1,0,0)),[](const auto& point){
                return std::abs(point.X()-std::sqrt(96-point.Z()*point.Z()))<tolerance*20;
            });
        });
        run("automatic normal chooses either side and ignores target order",[&] {
            for(double nearest:{-3.,3.,0.})for(double orientation:{-1.,1.}) {
                std::vector<p::ProjectionTarget> targets{{plane(8),"far-front"},{plane(-9),"far-back"},{plane(nearest),"nearest"}};
                for(int order=0;order<2;++order) {
                    const auto result=project(source,targets,source_plane,gp_Dir(0,0,orientation),true);
                    check(result,[&](const auto& point){return std::abs(point.Z()-nearest)<tolerance;});
                    for(const auto& curve:result.curves)require(curve.target_keys==std::vector<std::string>{"nearest"},"Automatic normal selected a farther face");
                    std::ranges::reverse(targets);
                }
            }
        });
        run("automatic normal rejects an equal-distance opposite pair",[&] {
            require(project(source,{{plane(3),"front"},{plane(-3),"back"}},source_plane,gp_Dir(0,0,1),true).ambiguous,
                "Opposite equidistant targets were silently chosen");
        });
        run("automatic normal ignores an occluded equal-distance pair",[&] {
            const auto result=project(source,{{plane(3),"front"},{plane(-3),"back"},{plane(1),"near"}},source_plane,gp_Dir(0,0,1),true);
            check(result,[](const auto& point){return std::abs(point.Z()-1)<tolerance;});
        });
        run("automatic normal rejects an isolated nearest-side switch",[&] {
            const auto sloped=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,-8),gp_Dir(-.5,0,1)),-20,20,-20,20).Face();
            require(project(source,{{plane(8),"front"},{sloped,"sloped-back"}},source_plane,gp_Dir(0,0,1),true).ambiguous,
                "Nearest-side switch was missed between classification samples");
        });
        run("nearest face independent of target row order",[&] {
            std::vector<p::ProjectionTarget> targets{{plane(8),"far"},{plane(3),"near"}};
            for(int i=0;i<2;++i) {
                auto result=project(source,targets);
                check(result,[](const auto& p){return std::abs(p.Z()-3)<tolerance && std::abs(p.Y()-2)<tolerance;});
                for(const auto& piece:result.curves)require(piece.target_keys==std::vector<std::string>{"near"},"Wrong nearest parent");
                std::ranges::reverse(targets);
            }
        });
        run("partly occluding face divides the source domain",[&] {
            auto result=project(source,{{plane(8),"far"},{plane(3,-2,2),"near"}});
            check(result,[](const auto& p){return std::abs(p.Y()-2)<tolerance &&
                (std::abs(p.X())<=2+tolerance?std::abs(p.Z()-3)<tolerance||std::abs(p.Z()-8)<tolerance:std::abs(p.Z()-8)<tolerance);});
            require(result.curves.size()==3,"Occlusion must create three source intervals");
        });
        run("bounded target leaves explicit missing intervals",[&] {
            auto result=project(source,{{plane(3,-2,2),"bounded"}});
            check(result,[](const auto& p){return std::abs(p.X())<=2+tolerance&&std::abs(p.Z()-3)<tolerance;},false);
            require(result.gaps.size()==2,"Missing intervals were not reported");
        });
        run("negative targets are not silently used",[&] {
            auto result=project(source,{{plane(-3),"behind"}});
            require(result.curves.empty()&&result.gaps.size()==1,"Backward hit accepted");
            auto reversed=project(source,{{plane(-3),"behind"}},gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),gp_Dir(0,0,-1));
            check(reversed,[](const auto& p){return std::abs(p.Z()+3)<tolerance;});
        });
        run("coincident start has zero depth without a shifted Sketch",[&] {
            auto result=project(source,{{plane(0),"start"},{plane(8),"far"}});
            check(result,[](const auto& p){return std::abs(p.Z())<tolerance;});
            for(const auto& piece:result.curves)require(piece.target_keys==std::vector<std::string>{"start"},"Zero hit lost");
        });
        run("intersecting targets switch first hit at their crossing",[&] {
            const auto tilted=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,5),gp_Dir(-1,0,1)),-20,20,-20,20).Face();
            auto result=project(source,{{plane(5),"horizontal"},{tilted,"sloped"}});
            check(result,[](const auto& p){return std::abs(p.Z()-(p.X()<-5-tolerance?5.:std::min(5.,5.+p.X())))<tolerance*8 ||
                (std::abs(p.X()+5)<tolerance*8&&std::abs(p.Z()-5)<tolerance*8);});
        });
        run("sphere uses front root",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeSphere(10).Shape(),GeomAbs_Sphere);
            const auto curve=line({-6,2,-20},{6,2,-20});
            auto result=project(curve,{{target,"sphere"}},gp_Pln(gp_Pnt(0,0,-20),gp_Dir(0,0,1)));
            check(result,[](const auto& p){return std::abs(p.Z()+std::sqrt(100-p.X()*p.X()-4))<tolerance*8;});
        });
        run("cylinder uses front root",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeCylinder(10,10).Shape(),GeomAbs_Cylinder);
            auto result=project(line({-20,-3,5},{-20,3,5}),{{target,"cylinder"}},
                gp_Pln(gp_Pnt(-20,0,0),gp_Dir(1,0,0)),gp_Dir(1,0,0));
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance && std::abs(p.X()+std::sqrt(100-p.Y()*p.Y()))<tolerance*8;});
        });
        run("cone uses actual station radius",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeCone(8,4,10).Shape(),GeomAbs_Cone);
            auto result=project(line({-20,-3,5},{-20,3,5}),{{target,"cone"}},
                gp_Pln(gp_Pnt(-20,0,0),gp_Dir(1,0,0)),gp_Dir(1,0,0));
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance && std::abs(p.X()+std::sqrt(36-p.Y()*p.Y()))<tolerance*8;});
        });
        run("doubly curved native patch retains exact surface support",[&] {
            NCollection_Array2<gp_Pnt> poles(1,3,1,3);
            const double x[3]{-5,0,5},square[3]{25,-25,25};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)poles.SetValue(i+1,j+1,gp_Pnt(x[i],x[j],5+.1*square[i]+.05*square[j]));
            Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(poles);
            auto target=BRepBuilderAPI_MakeFace(surface,tolerance).Face();
            auto result=project(line({-4,1,0},{4,1,0}),{{target,"paraboloid"}});
            check(result,[](const auto& p){return std::abs(p.Y()-1)<tolerance&&std::abs(p.Z()-(5+.1*p.X()*p.X()+.05))<tolerance*8;});
        });
        run("inclined Sketch projects onto one or several curved B-spline faces",[&] {
            // Exact biquadratic B-spline: z = 5 + .05*x*x + .025*y*y.
            // The Sketch is tilted independently of the target, and is above it.
            NCollection_Array2<gp_Pnt> poles(1,3,1,3);
            const double x[3]{-10,0,10},square[3]{100,-100,100};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)
                poles(i+1,j+1)={x[i],x[j],5+.05*square[i]+.025*square[j]};
            NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;
            NCollection_Array1<int> multiplicities(1,2);multiplicities(1)=multiplicities(2)=3;
            Handle(Geom_BSplineSurface) surface=new Geom_BSplineSurface(poles,knots,knots,multiplicities,multiplicities,2,2);
            const auto whole=BRepBuilderAPI_MakeFace(surface,tolerance).Face();
            const auto left=BRepBuilderAPI_MakeFace(surface,0,.5,0,1,tolerance).Face();
            const auto right=BRepBuilderAPI_MakeFace(surface,.5,1,0,1,tolerance).Face();
            const auto edge=line({-2,1,20.6},{8,1,17.6});
            const gp_Pln sketch(gp_Pnt(0,0,20),gp_Dir(.3,0,1));
            for(bool split:{false,true})for(double orientation:{-1.,1.}) {
                std::vector<p::ProjectionTarget> targets=split?
                    std::vector<p::ProjectionTarget>{{left,"left-spline"},{right,"right-spline"}}:
                    std::vector<p::ProjectionTarget>{{whole,"whole-spline"}};
                // A farther selected face must not change the chosen result.
                targets.push_back({plane(-8),"farther-plane"});
                for(int order=0;order<2;++order) {
                    const auto result=project(edge,targets,sketch,gp_Dir(.3*orientation,0,orientation),true);
                    check(result,[](const auto& point){return std::abs(point.Y()-1)<tolerance*8&&
                        std::abs(point.Z()-5-.05*point.X()*point.X()-.025)<tolerance*8;});
                    double coverage=0;bool hit_left=false,hit_right=false;
                    for(const auto& piece:result.curves) {
                        coverage+=piece.source_last-piece.source_first;
                        for(const auto& key:piece.target_keys) {
                            require(key!="farther-plane","Farther face replaced the curved surface");
                            hit_left|=key=="left-spline";hit_right|=key=="right-spline";
                        }
                    }
                    const BRepAdaptor_Curve input(edge);
                    require(std::abs(coverage-input.LastParameter()+input.FirstParameter())<tolerance,
                        "B-spline face boundary lost or duplicated source coverage");
                    if(split)require(hit_left&&hit_right,"Projection failed to cross the two B-spline faces");
                    // Independent analytic ray/surface intersection, including the seam.
                    for(int i=0;i<=80;++i) {
                        const double s=-2+10.*i/80,b=.03*s-1,c=.05*s*s+.3*s-14.975;
                        const double t=(-b-std::sqrt(b*b-.018*c))/.009;
                        const gp_Pnt expected(s+.3*t,1,20-.3*s+t);
                        double distance=1e10;
                        for(const auto& piece:result.curves) {
                            const BRepAdaptor_Curve actual(piece.edge);
                            GeomAPI_ProjectPointOnCurve on(expected,actual.Curve().Curve(),actual.FirstParameter(),actual.LastParameter());
                            if(on.NbPoints())distance=std::min(distance,on.LowerDistance());
                        }
                        require(distance<tolerance*8,"Inclined B-spline projection disagrees with the independent ray equation");
                    }
                    std::ranges::reverse(targets);
                }
            }
        });
        run("coincident independent target interiors are ambiguous",[&] {
            auto result=project(source,{{plane(3),"first"},{plane(3),"second"}});
            require(result.ambiguous&&result.curves.empty(),"Coincident targets selected by traversal order");
        });
        run("full circle keeps its closed source domain",[&] {
            const auto circle=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4)).Edge();
            auto result=project(circle,{{plane(5),"plane"}});
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance&&std::abs(p.X()*p.X()+p.Y()*p.Y()-16)<tolerance*8;});
            require(result.curves.size()==1,"A full circle must retain one native curve");
            require(std::abs(result.curves[0].source_last-result.curves[0].source_first-2*std::acos(-1.))<tolerance,"Closed domain lost");
        });
        run("closed source on a spline plane with a partial occluder",[&] {
            NCollection_Array2<gp_Pnt> poles(1,2,1,2);
            poles(1,1)={-10,-10,8};poles(2,1)={10,-10,8};poles(1,2)={-10,10,8};poles(2,2)={10,10,8};
            Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(poles);
            const auto target=BRepBuilderAPI_MakeFace(surface,tolerance).Face();
            const auto circle=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4)).Edge();
            const auto result=project(circle,{{target,"spline-plane"},{plane(3,-2,2),"near"}});
            check(result,[](const auto& p){return std::abs(p.X()*p.X()+p.Y()*p.Y()-16)<tolerance*8&&
                (std::abs(p.X())<2-tolerance?std::abs(p.Z()-3)<tolerance:std::abs(p.X())>2+tolerance?
                    std::abs(p.Z()-8)<tolerance:std::abs(p.Z()-3)<tolerance||std::abs(p.Z()-8)<tolerance);});
            double covered=0;for(const auto& piece:result.curves)covered+=piece.source_last-piece.source_first;
            require(std::abs(covered-2*std::acos(-1.))<1e-9,"Closed source coverage crossed its seam incorrectly");
        });
        run("ellipse and trimmed elliptic arc preserve equations",[&] {
            const gp_Elips ellipse(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),6,3);
            for(bool closed:{false,true}) {
                const auto edge=closed?BRepBuilderAPI_MakeEdge(ellipse).Edge():BRepBuilderAPI_MakeEdge(ellipse,.35,4.7).Edge();
                check(project(edge,{{plane(5),"plane"}}),[](const auto& p){return std::abs(p.Z()-5)<tolerance&&
                    std::abs(p.X()*p.X()/36+p.Y()*p.Y()/9-1)<tolerance*8;});
            }
        });
        run("native polynomial spline survives oblique direction",[&] {
            NCollection_Array1<gp_Pnt> poles(1,4);
            poles(1)={-5,-1,0};poles(2)={-2,4,0};poles(3)={2,-3,0};poles(4)={5,1,0};
            Handle(Geom_BezierCurve) curve=new Geom_BezierCurve(poles);
            const auto edge=BRepBuilderAPI_MakeEdge(curve).Edge();
            auto result=project(edge,{{plane(5),"plane"}},gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),gp_Dir(1,0,2));
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance;});
            for(const auto& piece:result.curves) {
                const BRepAdaptor_Curve projected(piece.edge);
                for(int i=0;i<=20;++i) {
                    const double u=i/20.;const auto expected=curve->Value(u).Translated(gp_Vec(2.5,0,5));
                    GeomAPI_ProjectPointOnCurve on(expected,projected.Curve().Curve(),projected.FirstParameter(),projected.LastParameter());
                    require(on.NbPoints()&&on.LowerDistance()<tolerance*8,"Oblique spline correspondence changed");
                }
            }
        });
        run("periodic cylinder seam is not a missing interval",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeCylinder(10,10).Shape(),GeomAbs_Cylinder);
            auto result=project(line({20,-3,5},{20,3,5}),{{target,"cylinder"}},
                gp_Pln(gp_Pnt(20,0,0),gp_Dir(1,0,0)),gp_Dir(-1,0,0));
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance&&std::abs(p.X()-std::sqrt(100-p.Y()*p.Y()))<tolerance*8;});
        });
        run("hole remains a gap unless a selected farther face exists",[&] {
            auto outer=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,5),gp_Dir(0,0,1)),9))).Wire();
            auto hole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,5),gp_Dir(0,0,1)),3))).Wire();
            hole.Reverse();BRepBuilderAPI_MakeFace face(outer);face.Add(hole);
            const auto edge=line({-6,0,0},{6,0,0});
            auto result=project(edge,{{face.Face(),"annulus"}});
            check(result,[](const auto& p){return std::abs(p.Z()-5)<tolerance&&std::abs(p.X())>=3-tolerance;},false);
            require(result.gaps.size()==1,"Hole was filled by projection");
            auto backed=project(edge,{{face.Face(),"annulus"},{plane(8),"backing"}});
            check(backed,[](const auto& p){return std::abs(p.X())>=3-tolerance?std::abs(p.Z()-5)<tolerance||std::abs(p.Z()-8)<tolerance:std::abs(p.Z()-8)<tolerance;});
        });
        run("shared physical boundary retains both face parents",[&] {
            auto result=project(line({0,-4,0},{0,4,0}),{{plane(5,-6,0),"left"},{plane(5,0,6),"right"}});
            check(result,[](const auto& p){return std::abs(p.X())<tolerance&&std::abs(p.Z()-5)<tolerance;});
            for(const auto& piece:result.curves)require(piece.target_keys==std::vector<std::string>{"left","right"},"Shared boundary parent lost");
        });
        run("translated and rotated source uses its real plane",[&] {
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.74);
            gp_Trsf translation;translation.SetTranslation(gp_Vec(19,-7,13));const auto transform=translation*rotation;
            const auto edge=TopoDS::Edge(BRepBuilderAPI_Transform(source,transform,true).Shape());
            const auto target=TopoDS::Face(BRepBuilderAPI_Transform(plane(5),transform,true).Shape());
            gp_Pln sketch(gp_Pnt(0,0,0),gp_Dir(0,0,1));sketch.Transform(transform);
            const auto direction=gp_Dir(0,0,1).Transformed(transform);
            check(project(edge,{{target,"transformed"}},sketch,direction),[&](const auto& p){
                const auto local=p.Transformed(transform.Inverted());return std::abs(local.Z()-5)<tolerance&&std::abs(local.Y()-2)<tolerance;
            });
        });
        run("tangent cylinder generator remains a genuine native curve",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeCylinder(10,10).Shape(),GeomAbs_Cylinder);
            auto result=project(line({-20,10,1},{-20,10,9}),{{target,"cylinder"}},
                gp_Pln(gp_Pnt(-20,0,0),gp_Dir(1,0,0)),gp_Dir(1,0,0));
            check(result,[](const auto& p){return std::abs(p.X())<tolerance*8&&std::abs(p.Y()-10)<tolerance*8;});
        });
        run("closed source projected onto cylinder covers both sides of its parameter seam",[&] {
            const auto target=analytical_face(BRepPrimAPI_MakeCylinder(10,10).Shape(),GeomAbs_Cylinder);
            const gp_Circ circle(gp_Ax2(gp_Pnt(-20,0,5),gp_Dir(1,0,0)),3);
            auto result=project(BRepBuilderAPI_MakeEdge(circle).Edge(),{{target,"cylinder"}},
                gp_Pln(gp_Pnt(-20,0,0),gp_Dir(1,0,0)),gp_Dir(1,0,0));
            check(result,[](const auto& p){return std::abs(p.X()+std::sqrt(100-p.Y()*p.Y()))<tolerance*8&&
                std::abs(p.Y()*p.Y()+(p.Z()-5)*(p.Z()-5)-9)<tolerance*30;});
            double coverage=0;for(const auto& piece:result.curves)coverage+=piece.source_last-piece.source_first;
            require(std::abs(coverage-2*std::acos(-1.))<tolerance,"Closed projection lost source coverage");
        });
        run("partial occlusion of closed curve retains the complete circle",[&] {
            const auto circle=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4)).Edge();
            auto result=project(circle,{{plane(8),"far"},{plane(3,-2,2),"near"}});
            check(result,[](const auto& p){return std::abs(p.X()*p.X()+p.Y()*p.Y()-16)<tolerance*30&&
                (std::abs(p.X())<=2+tolerance?std::abs(p.Z()-3)<tolerance||std::abs(p.Z()-8)<tolerance:std::abs(p.Z()-8)<tolerance);});
            double coverage=0;for(const auto& piece:result.curves)coverage+=piece.source_last-piece.source_first;
            require(std::abs(coverage-2*std::acos(-1.))<tolerance,"Occluded closed curve lost source coverage");
        });
        run("closed projection partitions a face into explicit regions",[&] {
            const auto circle=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),4)).Edge();
            std::vector<p::ProjectionTarget> targets{{plane(5,-10,10,-10,10),"face"}};
            const auto result=project(circle,targets);
            const auto split=p::partition_projection({},targets,result,tolerance);
            require(split.regions.size()==2,"Circle must create an inner and outer region");
            double total=0;bool disk=false;
            for(const auto& region:split.regions) {
                GProp_GProps area;BRepGProp::SurfaceProperties(region.face,area);total+=area.Mass();
                disk=disk||std::abs(area.Mass()-16*std::acos(-1.))<tolerance*100;
                require(region.target_key=="face","Region lost its target parent");
            }
            require(disk&&std::abs(total-400)<tolerance*100,"Partition areas are not independently correct");
        });
        run("open curve partitions only when reaching real boundaries",[&] {
            const std::vector<p::ProjectionTarget> targets{{plane(5,-10,10,-10,10),"face"}};
            const auto valid=project(line({-10,0,0},{10,0,0}),targets);
            require(p::partition_projection({},targets,valid,tolerance).regions.size()==2,"Boundary-assisted open partition rejected");
            bool rejected=false;
            try{const auto partial=project(line({-4,0,0},{4,0,0}),targets);p::partition_projection({},targets,partial,tolerance);}
            catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"A dangling open curve does not define a region");
        });
        run("independent closed or open curves partition the same face together",[&] {
            const std::vector<p::ProjectionTarget> targets{{plane(5,-10,10,-10,10),"target"}};
            const auto circle=[](double x) {
                return BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(x,0,0),gp_Dir(0,0,1)),2)).Edge();
            };
            for(bool closed:{true,false}) {
                const std::vector<p::ProjectionCurve> curves{
                    {closed?circle(-4):line({-10,-3,0},{10,-3,0}),"first"},
                    {closed?circle(4):line({-10,3,0},{10,3,0}),"second"}};
                const auto result=p::project_directionally(curves,targets,source_plane,gp_Dir(0,0,1),tolerance);
                require(!result.ambiguous&&result.gaps.empty()&&result.curves.size()==2,
                    "Projection omitted one of two independent source curves");
                auto regions=p::partition_projection({},targets,result,tolerance).regions;
                require(regions.size()==3,"Two independent source curves did not produce three regions");
                std::vector<double> areas;
                for(const auto& region:regions) {
                    GProp_GProps properties;
                    BRepGProp::SurfaceProperties(region.face,properties);
                    areas.push_back(properties.Mass());
                }
                std::ranges::sort(areas);
                const double disk=4*std::acos(-1.);
                const std::array expected=closed?std::array{disk,disk,400-2*disk}:std::array{120.,140.,140.};
                for(std::size_t i=0;i<areas.size();++i)
                    require(std::abs(areas[i]-expected[i])<tolerance*100,"Projection partition area disagrees with its analytic value");
                auto with_dangling=curves;
                with_dangling.push_back({line({-1,8,0},{1,8,0}),"dangling"});
                const auto outline=p::project_directionally(with_dangling,targets,source_plane,gp_Dir(0,0,1),tolerance);
                require(!outline.ambiguous&&outline.gaps.empty()&&outline.curves.size()==3,
                    "Curve output rejected an independent open line");
                bool rejected=false;
                try{static_cast<void>(p::partition_projection({},targets,outline,tolerance));}
                catch(const std::invalid_argument&){rejected=true;}
                require(rejected,"Partition silently ignored a dangling open line");
            }
        });
        run("split output preserves material volume and occupied space",[&] {
            const auto box=BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,5),20,20,6).Shape();
            TopoDS_Face top;
            for(TopExp_Explorer it(box,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface surface(face);
                if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Location().Z()-11)<tolerance&&
                    std::abs(surface.Plane().Axis().Direction().Z())>.99)top=face;
            }
            require(!top.IsNull(),"Missing top face in fixture");
            const std::vector<p::ProjectionTarget> targets{{top,"top"}};
            const auto circle=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,20),gp_Dir(0,0,1)),4)).Edge();
            const auto projection=project(circle,targets,gp_Pln(gp_Pnt(0,0,20),gp_Dir(0,0,1)),gp_Dir(0,0,-1));
            const auto split=p::partition_projection(box,targets,projection,tolerance);
            require(split.regions.size()==2,"Solid face was not split");
            GProp_GProps before,after;BRepGProp::VolumeProperties(box,before);BRepGProp::VolumeProperties(split.shape,after);
            require(std::abs(after.Mass()-2400)<tolerance*100&&std::abs(before.Mass()-after.Mass())<tolerance*100,"Split changed volume");
            require(before.CentreOfMass().Distance(after.CentreOfMass())<tolerance,"Split changed centroid");
            for(bool reverse:{false,true}) {
                BRepAlgoAPI_Cut difference(reverse?box:split.shape,reverse?split.shape:box);
                require(difference.IsDone(),"Material-space comparison failed");
                GProp_GProps difference_volume;BRepGProp::VolumeProperties(difference.Shape(),difference_volume);
                require(std::abs(difference_volume.Mass())<tolerance*100,"Split changed occupied space");
            }
        });
        run("derived identity follows native parents through repeated edits and row reversal",[&] {
            std::vector<std::string> expected;
            for(double half_width:{2.,3.,1.5})for(bool reverse:{false,true}) {
                p::ProjectionTarget near{plane(3,-half_width,half_width),"near"};
                near.boundaries={{line({-half_width,-20,3},{-half_width,20,3}),"left"},
                    {line({half_width,-20,3},{half_width,20,3}),"right"},
                    {line({-half_width,-20,3},{half_width,-20,3}),"bottom"},
                    {line({-half_width,20,3},{half_width,20,3}),"top"}};
                std::vector<p::ProjectionTarget> targets{{plane(8),"far"},near};
                if(reverse)std::ranges::reverse(targets);
                const std::vector<p::ProjectionCurve> sources{{source,"authored-curve","source-start","source-end"}};
                const gp_Pln sketch(gp_Pnt(0,0,0),gp_Dir(0,0,1));const gp_Dir direction(0,0,1);
                auto result=p::project_directionally(sources,targets,sketch,direction,tolerance);
                p::name_projected_curves(result,sources,targets,sketch,direction,tolerance);
                std::vector<std::string> keys;
                for(const auto& piece:result.curves) {
                    keys.push_back(piece.semantic_key);
                    require(!piece.first_point_key.empty()&&!piece.last_point_key.empty(),"Native endpoint identity missing");
                }
                require(keys.size()==3,"Named occlusion interval count changed");
                if(expected.empty())expected=keys;
                require(keys==expected,"Coordinates or target row order changed derived identity");
            }
        });
        run("tangent cylinder source crosses zero depth without an artificial endpoint",[&] {
            const double radius=99.699320241;
            const auto face=analytical_face(BRepPrimAPI_MakeCylinder(radius,150.6390984).Shape(),GeomAbs_Cylinder);
            const gp_Pln sketch(gp_Pnt(radius,0,0),gp_Dir(1,0,0));const gp_Dir direction(1,0,0);
            for(bool reversed:{false,true})for(double slope:{0.,.125}) {
                const gp_Pnt a(radius,38.544972477,57.),b(radius,-30.897193718,57.+slope*69.442166195);
                const auto edge=reversed?line(b,a):line(a,b);
                const std::vector<p::ProjectionCurve> sources{{edge,"tangent-source","start","end"}};
                const std::vector<p::ProjectionTarget> targets{{face,"cylinder"}};
                auto result=project(edge,targets,sketch,direction,true);
                if(result.curves.size()!=1)for(const auto& piece:result.curves) {
                    const BRepAdaptor_Curve curve(piece.edge);
                    std::cerr<<"tangent piece slope="<<slope<<" reversed="<<reversed<<" source="<<piece.source_first<<".."<<piece.source_last
                        <<" native="<<curve.FirstParameter()<<".."<<curve.LastParameter()<<" type="<<int(curve.GetType())<<'\n';
                }
                require(result.curves.size()==1&&!result.ambiguous&&result.gaps.empty(),"Zero-depth curtain split a continuous cylindrical projection");
                // The helper uses its own authored key when checking equations.
                result.curves.front().source_key="tangent-source";
                p::name_projected_curves(result,sources,targets,sketch,direction,tolerance);
                require(!result.curves.front().first_point_key.empty()&&!result.curves.front().last_point_key.empty(),"Tangent cylindrical projection lost authored endpoints");
            }
        });
        std::cout<<"Passed "<<passed<<" directional projection cases\n";
        return 0;
    } catch(const Standard_Failure& failure) {std::cerr<<"OCCT: "<<failure.GetMessageString()<<'\n';}
      catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';}
    return 1;
}
