#include <zima/kernel/occt_planar_edge_fillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    return std::abs(properties.Mass());
}

double area(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape,properties);
    return std::abs(properties.Mass());
}

void near(double actual,double expected,const char* message) {
    if(std::abs(actual-expected)>
            1.0e-7*std::max({1.,std::abs(actual),std::abs(expected)}))
        throw std::runtime_error(std::string(message)+": "+
            std::to_string(actual)+" != "+std::to_string(expected));
}

bool shares_endpoint(const TopoDS_Edge& first,const TopoDS_Edge& second) {
    TopoDS_Vertex first_a,first_b,second_a,second_b;
    TopExp::Vertices(first,first_a,first_b);
    TopExp::Vertices(second,second_a,second_b);
    return first_a.IsSame(second_a)||first_a.IsSame(second_b)||
        first_b.IsSame(second_a)||first_b.IsSame(second_b);
}

std::vector<TopoDS_Edge> vertical_edges(const TopoDS_Shape& shape,
        double height) {
    std::vector<TopoDS_Edge> result;
    for(TopExp_Explorer item(shape,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        const BRepAdaptor_Curve curve(edge);
        if(curve.GetType()!=GeomAbs_Line)continue;
        TopoDS_Vertex first_vertex,last_vertex;
        TopExp::Vertices(edge,first_vertex,last_vertex);
        if(first_vertex.IsNull()||last_vertex.IsNull())continue;
        const auto first=BRep_Tool::Pnt(first_vertex);
        const auto last=BRep_Tool::Pnt(last_vertex);
        if(std::abs(first.X()-last.X())<1.0e-9&&
           std::abs(first.Y()-last.Y())<1.0e-9&&
           std::abs(std::abs(first.Z()-last.Z())-height)<1.0e-9)
            if(std::ranges::none_of(result,[&](const auto& current) {
                    return current.IsSame(edge);
                }))result.push_back(edge);
    }
    return result;
}

TopoDS_Shape reference_fillet(const TopoDS_Shape& input,
        const std::vector<TopoDS_Edge>& edges,double radius) {
    BRepFilletAPI_MakeFillet fillet(input);
    for(const auto& edge:edges)fillet.Add(radius,edge);
    fillet.Build();
    require(fillet.IsDone()&&!fillet.Shape().IsNull(),
        "OCCT reference Fillet failed");
    require(BRepCheck_Analyzer(fillet.Shape()).IsValid(),
        "OCCT reference Fillet is invalid");
    return fillet.Shape();
}

void verify_result(const TopoDS_Shape& input,
        const std::vector<TopoDS_Edge>& edges,double radius) {
    const auto custom=zima::kernel::occt_planar_edge_fillet(
        input,edges,radius,1.0e-7);
    const auto reference=reference_fillet(input,edges,radius);
    require(!custom.shape.IsNull()&&custom.history&&
            custom.removal_domains.size()==edges.size()&&
            custom.analytic_removal_domains.size()==edges.size()&&
            std::ranges::all_of(custom.removal_domains,
                [](const auto& shape) {
                    return !shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();
                })&&
            custom.fillet_faces.size()==edges.size()&&
            std::ranges::all_of(custom.fillet_faces,
                [](const auto& faces){return !faces.empty();}),
        "Custom planar Fillet omitted shape, removal domain, history or parent faces");
    const auto distance=[](const auto& first,const auto& second) {
        return std::hypot(first.x-second.x,first.y-second.y,
            first.z-second.z);
    };
    const auto dot=[](const auto& first,const auto& second) {
        return first.x*second.x+first.y*second.y+first.z*second.z;
    };
    for(const auto& domain:custom.analytic_removal_domains) {
        const zima::kernel::Vec3 source_axis{
            domain.source_end.x-domain.source_start.x,
            domain.source_end.y-domain.source_start.y,
            domain.source_end.z-domain.source_start.z};
        require(std::abs(std::hypot(domain.axis_direction.x,
                    domain.axis_direction.y,domain.axis_direction.z)-1.)<
                    1.0e-10&&
                dot(source_axis,domain.axis_direction)>0.&&
                std::abs(distance(domain.center_at_start,
                    domain.first_tangent_at_start)-domain.radius)<1.0e-7&&
                std::abs(distance(domain.center_at_start,
                    domain.second_tangent_at_start)-domain.radius)<1.0e-7&&
                domain.axial_extension>=domain.radius,
            "Custom Fillet analytic removal domain is incomplete");
    }
    require(custom.metrics.requested_edges==edges.size()&&
            custom.metrics.cutter_solids==edges.size()&&
            custom.metrics.generated_fillet_faces>=edges.size(),
        "Custom planar Fillet metrics are incomplete");
    for(const auto& faces:custom.fillet_faces)
        for(const auto& face:faces) {
            const BRepAdaptor_Surface support(face,true);
            require(support.GetType()==GeomAbs_Cylinder&&
                    std::abs(support.Cylinder().Radius()-radius)<1.0e-7,
                "Custom Fillet face is not the requested exact cylinder");
            TopTools_IndexedMapOfShape boundary_edges;
            TopExp::MapShapes(face,TopAbs_EDGE,boundary_edges);
            require(boundary_edges.Extent()==4,
                "Custom Fillet face is not bounded by four exact edges");
            std::size_t line_count{},circle_count{};
            for(int index=1;index<=boundary_edges.Extent();++index) {
                switch(BRepAdaptor_Curve(TopoDS::Edge(
                        boundary_edges.FindKey(index))).GetType()) {
                case GeomAbs_Line:++line_count;break;
                case GeomAbs_Circle:++circle_count;break;
                default:break;
                }
            }
            require(line_count==2&&circle_count==2,
                "Custom Fillet face boundary lost its two lines and two circular arcs");
        }
    for(std::size_t index=0;index<edges.size();++index) {
        bool generated_face_found=false;
        for(const auto& descendant:custom.Generated(edges[index]))
            if(descendant.ShapeType()==TopAbs_FACE&&
               BRepAdaptor_Surface(TopoDS::Face(descendant),true).GetType()==
                   GeomAbs_Cylinder)
                generated_face_found=true;
        require(generated_face_found,
            "Custom Fillet adapter omitted edge-to-face ancestry");
    }
    near(volume(custom.shape),volume(reference),
        "Custom planar Fillet volume differs from OCCT");
    near(area(custom.shape),area(reference),
        "Custom planar Fillet area differs from OCCT");
    TopTools_IndexedMapOfShape custom_edges,reference_edges,
        custom_vertices,reference_vertices;
    TopExp::MapShapes(custom.shape,TopAbs_EDGE,custom_edges);
    TopExp::MapShapes(reference,TopAbs_EDGE,reference_edges);
    TopExp::MapShapes(custom.shape,TopAbs_VERTEX,custom_vertices);
    TopExp::MapShapes(reference,TopAbs_VERTEX,reference_vertices);
    require(custom_edges.Extent()==reference_edges.Extent(),
        "Custom Fillet changed the OCCT topological edge count");
    require(custom_vertices.Extent()==reference_vertices.Extent(),
        "Custom Fillet changed the OCCT topological vertex count");
}

TopoDS_Shape triangular_prism(double angle_degrees,double length,
        double height) {
    const double angle=angle_degrees*std::numbers::pi/180.;
    const gp_Pnt origin(0,0,0),first(length,0,0),
        second(length*std::cos(angle),length*std::sin(angle),0);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(origin,first).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(first,second).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(second,origin).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,height),false,true);
    prism.Build();
    require(prism.IsDone(),"Triangular prism fixture failed");
    return prism.Shape();
}

} // namespace

int main() try {
    {
        const auto box=BRepPrimAPI_MakeBox(20,30,40).Shape();
        const auto vertical=vertical_edges(box,40);
        require(vertical.size()==4,
            "Box fixture did not expose four vertical edges");
        verify_result(box,{vertical.front()},3.);
        verify_result(box,vertical,2.);
        TopoDS_Edge adjacent;
        for(TopExp_Explorer item(box,TopAbs_EDGE);item.More();item.Next()) {
            const auto candidate=TopoDS::Edge(item.Current());
            if(!candidate.IsSame(vertical.front())&&
               shares_endpoint(candidate,vertical.front())) {
                adjacent=candidate;break;
            }
        }
        require(!adjacent.IsNull(),
            "Box fixture did not expose an adjacent edge");
        bool adjacent_rejected=false;
        try {
            const std::array selected{vertical.front(),adjacent};
            static_cast<void>(zima::kernel::occt_planar_edge_fillet(
                box,selected,2.,1.0e-7));
        } catch(const std::invalid_argument&) {
            adjacent_rejected=true;
        }
        require(adjacent_rejected,
            "Custom planar Fillet accepted selected edges sharing an endpoint");
    }
    {
        const auto wedge=triangular_prism(60.,30.,35.);
        const auto vertical=vertical_edges(wedge,35.);
        require(vertical.size()==3,
            "Wedge fixture did not expose three vertical edges");
        const auto origin=std::ranges::find_if(vertical,[](const auto& edge) {
            const BRepAdaptor_Curve curve(edge);
            const auto point=curve.Value(curve.FirstParameter());
            return std::abs(point.X())<1.0e-9&&
                std::abs(point.Y())<1.0e-9;
        });
        require(origin!=vertical.end(),
            "Wedge fixture lost its 60-degree edge");
        verify_result(wedge,{*origin},2.5);
    }
    {
        const auto cylinder=BRepPrimAPI_MakeCylinder(10,20).Shape();
        TopoDS_Edge curved;
        for(TopExp_Explorer edge(cylinder,TopAbs_EDGE);
                edge.More();edge.Next()) {
            const auto candidate=TopoDS::Edge(edge.Current());
            if(BRepAdaptor_Curve(candidate).GetType()==GeomAbs_Circle) {
                curved=candidate;break;
            }
        }
        require(!curved.IsNull(),"Cylinder fixture lost its circular rim");
        bool rejected=false;
        try {
            const std::array edges{curved};
            static_cast<void>(zima::kernel::occt_planar_edge_fillet(
                cylinder,edges,1.,1.0e-7));
        } catch(const std::invalid_argument&) {
            rejected=true;
        }
        require(rejected,
            "Planar Fillet accepted a curved or non-plane/plane edge");
    }
    std::cout<<"Custom planar edge Fillet: 90-degree single/batch, 60-degree, exact cylinder ancestry and rejection checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
