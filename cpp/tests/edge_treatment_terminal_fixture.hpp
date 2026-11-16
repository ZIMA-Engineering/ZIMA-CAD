#pragma once

#include <zima/kernel/occt_planar_edge_fillet.hpp>
#include <zima/kernel/occt_planar_edge_chamfer.hpp>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::test {

inline void verify_no_cut_at_rounded_lip(bool fillet) {
    const auto require=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
        return properties.Mass();
    };
    BRepAlgoAPI_Cut pocket(BRepPrimAPI_MakeBox(30,30,30).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(9,9,18),12,12,15).Shape());
    pocket.Build();require(pocket.IsDone(),"Material-exit pocket failed");
    TopoDS_Edge lip;
    for(TopExp_Explorer item(pocket.Shape(),TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());const BRepAdaptor_Curve c(edge);
        const auto a=c.Value(c.FirstParameter()),b=c.Value(c.LastParameter());
        if(std::abs(a.X()-9)<1.e-7&&std::abs(b.X()-9)<1.e-7&&
           std::abs(a.Z()-30)<1.e-7&&std::abs(b.Z()-30)<1.e-7)lip=edge;
    }
    require(!lip.IsNull(),"Material-exit pocket lost its lip");
    const auto input=kernel::occt_planar_edge_fillet(pocket.Shape(),std::array{lip},3.,1.e-7).shape;
    TopoDS_Edge vertical;
    for(TopExp_Explorer item(input,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());const BRepAdaptor_Curve c(edge);
        const auto a=c.Value(c.FirstParameter()),b=c.Value(c.LastParameter());
        if(c.GetType()==GeomAbs_Line&&std::abs(a.X()-9)<1.e-7&&std::abs(b.X()-9)<1.e-7&&
           std::abs(a.Y()-9)<1.e-7&&std::abs(b.Y()-9)<1.e-7)vertical=edge;
    }
    require(!vertical.IsNull(),"Material-exit pocket lost its vertical corner");
    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
    TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(const bool reverse:{false,true})for(int variant=0;variant<(fillet?1:5);++variant) {
        auto edge=vertical;if(reverse)edge.Reverse();
        kernel::OcctPlanarEdgeEndFaces end_control;
        TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last,false);
        (BRep_Tool::Pnt(first).Z()>BRep_Tool::Pnt(last).Z()?
            end_control.automatic_first:end_control.automatic_last)=false;
        const auto calculate=[&](kernel::OcctPlanarEdgeEndMode ends) {
            if(fillet)return kernel::occt_planar_edge_fillet(input,std::array{edge},2.,1.e-7,
                std::array{end_control},ends).shape;
            kernel::OcctPlanarEdgeChamferSelection selected{edge};
            auto it=adjacency.FindFromKey(edge).cbegin();
            if(variant%2==0)++it;
            selected.distance_a_face=TopoDS::Face(*it);
            using Mode=kernel::OcctPlanarEdgeChamferMode;
            return kernel::occt_planar_edge_chamfer(input,std::array{selected},
                variant==0?Mode::EqualDistance:variant<=2?Mode::TwoDistances:Mode::DistanceAngle,
                2.,variant<=2?1.:std::numbers::pi/6.,1.e-7,std::array{end_control}, {},ends).shape;
        };
        const auto automatic=calculate(kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope);
        const auto flat=calculate(kernel::OcctPlanarEdgeEndMode::Perpendicular);
        require(BRepCheck_Analyzer(automatic).IsValid(),"Material-exit treatment is invalid");
        require(std::abs(volume(automatic)-volume(flat))<1.e-6,
            "NoCut extended an additive treatment beyond the rounded lip");
    }
}

// A tangent line/arc/line route with two oblique terminal faces. At the
// concave foot the exact end adds material past the perpendicular section,
// but none may survive on the far side of the first terminal plane.
inline void verify_oblique_treatment_terminals(bool fillet) {
    verify_no_cut_at_rounded_lip(fillet);
    const auto require=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps props;BRepGProp::VolumeProperties(shape,props);
        return std::abs(props.Mass());
    };
    const gp_Pnt p0(40,0,0),p1(10,0,0),p2(0,10,0),p3(0,40,0),p4(35,35,0);
    BRepBuilderAPI_MakeWire outline;
    outline.Add(BRepBuilderAPI_MakeEdge(p0,p1).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(gp_Circ(
        gp_Ax2(gp_Pnt(10,10,0),gp_Dir(0,0,1)),10.),
        std::numbers::pi,1.5*std::numbers::pi).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(p2,p3).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(p3,p4).Edge());
    outline.Add(BRepBuilderAPI_MakeEdge(p4,p0).Edge());
    require(outline.IsDone(),"Oblique terminal outline failed");
    const auto boss=BRepPrimAPI_MakePrism(
        BRepBuilderAPI_MakeFace(outline.Wire()).Face(),gp_Vec(0,0,10)).Shape();
    if(fillet) {
        // A crossed cylindrical Body boundary is different from a previous
        // tangent radius. The removal wedge still contains material just
        // beyond the sharp endpoint, so a wedge probe alone cannot decide.
        BRepAlgoAPI_Common clipped(boss,BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(10,10,0),gp_Dir(0,0,1)),25.,10.).Shape());
        clipped.Build();require(clipped.IsDone(),"Curved terminal fixture failed");
        const auto input=clipped.Shape();
        std::array<TopoDS_Edge,3> route;
        TopoDS_Face cap;
        for(TopExp_Explorer item(input,TopAbs_FACE);item.More();item.Next()) {
            const auto face=TopoDS::Face(item.Current());
            const BRepAdaptor_Surface surface(face);
            if(surface.GetType()==GeomAbs_Cylinder&&
               std::abs(surface.Cylinder().Radius()-25.)<1.e-7)cap=face;
        }
        for(TopExp_Explorer item(input,TopAbs_EDGE);item.More();item.Next()) {
            const auto edge=TopoDS::Edge(item.Current());const BRepAdaptor_Curve curve(edge);
            const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
            if(std::abs(a.Z()-10.)>1.e-7||std::abs(b.Z()-10.)>1.e-7)continue;
            if(curve.GetType()==GeomAbs_Circle&&std::abs(curve.Circle().Radius()-10.)<1.e-7)route[1]=edge;
            else if(std::abs(a.Y())<1.e-7&&std::abs(b.Y())<1.e-7)route[0]=edge;
            else if(std::abs(a.X())<1.e-7&&std::abs(b.X())<1.e-7)route[2]=edge;
        }
        require(!cap.IsNull()&&std::ranges::none_of(route,[](const auto& e){return e.IsNull();}),
            "Curved terminal fixture lost its route or envelope");
        for(const double radius:{1.,3.}) {
            std::array<kernel::OcctPlanarEdgeEndFaces,3> selected;
            for(const std::size_t index:{0u,2u}) {
                TopoDS_Vertex first,last;TopExp::Vertices(route[index],first,last,false);
                const auto point=BRep_Tool::Pnt(first);
                (point.X()>30.||point.Y()>30.?selected[index].first:selected[index].last)=cap;
            }
            const auto automatic=kernel::occt_planar_edge_fillet(input,route,radius,1.e-7,{},
                kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope).shape;
            const auto explicit_cut=kernel::occt_planar_edge_fillet(input,route,radius,1.e-7,selected,
                kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope).shape;
            const auto flat=kernel::occt_planar_edge_fillet(input,route,radius,1.e-7).shape;
            require(BRepCheck_Analyzer(automatic).IsValid()&&
                    std::abs(volume(automatic)-volume(explicit_cut))<1.e-5,
                "A continuous route ignored its first curved Body boundary");
            require(volume(automatic)<volume(flat)-1.e-4,
                "A curved Body terminal silently behaved as NoCut");
        }
    }
    for(const bool additive:{true,false}) {
        TopoDS_Shape input=boss;
        if(additive) {
            BRepAlgoAPI_Fuse joined(boss,
                BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,-10),70,70,10).Shape());
            joined.Build();require(joined.IsDone(),"Oblique terminal base fuse failed");
            input=joined.Shape();
        }
        require(BRepCheck_Analyzer(input).IsValid(),"Oblique terminal input invalid");
        const double height=additive?0.:10.;
        std::array<TopoDS_Edge,3> route;
        for(TopExp_Explorer item(input,TopAbs_EDGE);item.More();item.Next()) {
            const auto edge=TopoDS::Edge(item.Current());
            const BRepAdaptor_Curve curve(edge);
            const auto first=curve.Value(curve.FirstParameter());
            const auto last=curve.Value(curve.LastParameter());
            if(std::abs(first.Z()-height)>1.e-7||std::abs(last.Z()-height)>1.e-7)continue;
            if(curve.GetType()==GeomAbs_Circle)route[1]=edge;
            else if(std::abs(first.Y())<1.e-7&&std::abs(last.Y())<1.e-7)route[0]=edge;
            else if(std::abs(first.X())<1.e-7&&std::abs(last.X())<1.e-7)route[2]=edge;
        }
        require(std::ranges::none_of(route,[](const auto& e){return e.IsNull();}),
            "Oblique terminal route missing");
        const std::array planes{
            gp_Pln(gp_Pnt(40,0,height),gp_Dir(7,1,0)),
            gp_Pln(gp_Pnt(0,40,height),gp_Dir(1,7,0))};
        std::array<TopoDS_Face,2> caps;
        for(TopExp_Explorer item(input,TopAbs_FACE);item.More();item.Next()) {
            const auto face=TopoDS::Face(item.Current());
            const BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Plane)continue;
            for(std::size_t n=0;n<2;++n)
                if(surface.Plane().Distance(planes[n].Location())<1.e-7&&
                   std::abs(surface.Plane().Axis().Direction().Dot(
                       planes[n].Axis().Direction()))>1.-1.e-7)caps[n]=face;
        }
        require(!caps[0].IsNull()&&!caps[1].IsNull(),"Oblique terminal caps missing");
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(input,TopAbs_EDGE,TopAbs_FACE,adjacency);
        for(const double size:{1.,3.})for(const bool reversed:{false,true})
          for(int variant=0;variant<(fillet?1:9);++variant) {
            using Mode=kernel::OcctPlanarEdgeChamferMode;
            const auto mode=variant==0?Mode::EqualDistance:
                variant<=4?Mode::TwoDistances:Mode::DistanceAngle;
            const bool flip=variant>0&&variant%2==0;
            const double secondary=mode==Mode::TwoDistances?
                size*(variant<=2?.5:2.):mode==Mode::DistanceAngle?
                (variant<=6?30.:60.)*std::numbers::pi/180.:size;
            auto edges=route;
            if(reversed)for(auto& edge:edges)edge.Reverse();
            std::array<kernel::OcctPlanarEdgeChamferSelection,3> selections;
            for(std::size_t index=0;index<edges.size();++index) {
                selections[index].edge=edges[index];
                for(const auto& shape:adjacency.FindFromKey(edges[index])) {
                    const auto face=TopoDS::Face(shape);
                    const BRepAdaptor_Surface surface(face);
                    const bool horizontal=surface.GetType()==GeomAbs_Plane&&
                        std::abs(surface.Plane().Axis().Direction().Z())>1.-1.e-7;
                    if(horizontal!=flip)selections[index].distance_a_face=face;
                }
            }
            std::array<kernel::OcctPlanarEdgeEndFaces,3> automatic;
            auto explicit_ends=automatic,perpendicular=automatic;
            for(auto& end:perpendicular)end.automatic_first=end.automatic_last=false;
            for(const std::size_t n:{0u,1u}) {
                const std::size_t index=n==0?0:2;
                TopoDS_Vertex first,last;TopExp::Vertices(edges[index],first,last,false);
                const bool at_first=BRep_Tool::Pnt(first).Distance(planes[n].Location())<1.e-7;
                (at_first?explicit_ends[index].first:explicit_ends[index].last)=caps[n];
            }
            const auto calculate=[&](const auto& ends) {
                if(fillet)return kernel::occt_planar_edge_fillet(input,edges,size,1.e-7,
                    ends,kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope).shape;
                return kernel::occt_planar_edge_chamfer(input,selections,mode,size,secondary,1.e-7,
                    ends,{},kernel::OcctPlanarEdgeEndMode::AutomaticBodyEnvelope).shape;
            };
            const auto result=calculate(automatic),selected=calculate(explicit_ends),
                flat=calculate(perpendicular);
            require(BRepCheck_Analyzer(result).IsValid(),"Automatic oblique terminal invalid");
            require(std::abs(volume(result)-volume(selected))<1.e-5,
                "Automatic and selected oblique terminal disagree");
            if(!fillet) {
                const double b=mode==Mode::DistanceAngle?size*std::tan(secondary):secondary;
                const double horizontal=flip?b:size,vertical=flip?size:b;
                for(const auto& point:std::array{
                        gp_Pnt(25,additive?-horizontal:horizontal,height),
                        gp_Pnt(25,0,height+(additive?vertical:-vertical))})
                    require(BRepClass3d_SolidClassifier(result,point,1.e-7).State()==TopAbs_ON,
                        "Terminal trim changed the authored A/B or angle/FLIP section");
            }
            BRepAlgoAPI_Cut changed(additive?result:input,additive?input:result);
            changed.Build();require(changed.IsDone(),"Terminal changed-volume comparison failed");
            require(volume(changed.Shape())>1.,"Treatment changed no material");
            for(const auto& plane:planes) {
                const auto face=BRepBuilderAPI_MakeFace(plane).Face();
                const auto beyond=BRepPrimAPI_MakeHalfSpace(face,
                    plane.Location().Translated(gp_Vec(plane.Axis().Direction()))).Solid();
                BRepAlgoAPI_Common outside(changed.Shape(),beyond);outside.Build();
                require(outside.IsDone()&&volume(outside.Shape())<1.e-6,
                    "Treatment changed material beyond its first terminal plane");
            }
            if(additive) {
                require(volume(result)>volume(flat)+1.e-4,
                    "Additive Cut did not extend to its oblique terminal plane");
                for(const std::size_t disabled:{0u,1u}) {
                    auto one_cut=automatic;
                    const std::size_t index=disabled==0?0:2;
                    TopoDS_Vertex first,last;TopExp::Vertices(edges[index],first,last,false);
                    const bool at_first=BRep_Tool::Pnt(first).Distance(
                        planes[disabled].Location())<1.e-7;
                    (at_first?one_cut[index].automatic_first:
                        one_cut[index].automatic_last)=false;
                    const auto one_result=calculate(one_cut);
                    BRepAlgoAPI_Cut added(one_result,input);added.Build();
                    require(added.IsDone(),"One-ended treatment comparison failed");
                    for(const std::size_t end:{0u,1u}) {
                        const auto beyond=BRepPrimAPI_MakeBox(end==0?
                            gp_Pnt(40.000001,-100,-100):gp_Pnt(-100,40.000001,-100),
                            200,200,200).Shape();
                        BRepAlgoAPI_Common extension(added.Shape(),beyond);extension.Build();
                        require(extension.IsDone(),"One-ended extension comparison failed");
                        require(end==disabled?volume(extension.Shape())<1.e-6:
                            volume(extension.Shape())>1.e-4,
                            "Cut/NoCut affected the wrong physical endpoint");
                    }
                }
            }
        }
    }
}
} // namespace zima::test
