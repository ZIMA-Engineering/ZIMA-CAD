#include <zima/kernel/occt_boundary_union.hpp>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepAdaptor_Surface.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using zima::kernel::OcctBoundaryFace;
using zima::kernel::OcctBoundaryRegion;
using zima::kernel::OcctBoundarySolid;

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

OcctBoundarySolid input(const TopoDS_Shape& shape,
        const std::string& owner,std::size_t history_index) {
    OcctBoundarySolid result;
    result.shape=shape;
    TopTools_IndexedMapOfShape faces,edges,vertices;
    TopExp::MapShapes(shape,TopAbs_FACE,faces);
    TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);
    for(int index=1;index<=faces.Extent();++index) {
        const auto semantic="face:"+std::to_string(index-1);
        result.faces.push_back({TopoDS::Face(faces(index)),
            {{owner,semantic,{}},{owner+":alias",semantic,{}}},
            history_index});
    }
    for(int index=1;index<=edges.Extent();++index) {
        const auto semantic="edge:"+std::to_string(index-1);
        result.edges.push_back({TopoDS::Edge(edges(index)),
            {{owner,semantic,{}},{owner+":alias",semantic,{}}},
            history_index});
    }
    for(int index=1;index<=vertices.Extent();++index) {
        const auto semantic="vertex:"+std::to_string(index-1);
        result.vertices.push_back({TopoDS::Vertex(vertices(index)),
            {{owner,semantic,{}},{owner+":alias",semantic,{}}},
            history_index});
    }
    return result;
}

double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    return std::abs(properties.Mass());
}

double material_difference(const TopoDS_Shape& left,
        const TopoDS_Shape& right,double tolerance) {
    BRepAlgoAPI_Cut cut(left,right);
    cut.SetRunParallel(true);
    cut.SetFuzzyValue(tolerance);
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull(),
        "Boundary-union material comparison failed");
    return volume(cut.Shape());
}

TopoDS_Compound compound(std::initializer_list<TopoDS_Shape> members) {
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for(const auto& member:members)builder.Add(result,member);
    return result;
}

template <typename Reference>
std::string encoded_reference(const Reference& reference) {
    return std::to_string(reference.owner_id.size())+":"+
        reference.owner_id+std::to_string(reference.semantic_key.size())+":"+
        reference.semantic_key+std::to_string(reference.instance_path.size())+
        ":"+reference.instance_path;
}

std::string encoded_set(const std::set<std::string>& values) {
    std::string result;
    for(const auto& value:values)
        result+=std::to_string(value.size())+":"+value;
    return result;
}

TopoDS_Face planar_face_at(const TopoDS_Shape& shape,int axis,double value) {
    for(TopExp_Explorer explorer(shape,TopAbs_FACE);explorer.More();
            explorer.Next()) {
        const auto face=TopoDS::Face(explorer.Current());
        const BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto location=surface.Plane().Location();
        const auto direction=surface.Plane().Axis().Direction();
        const double coordinate=axis==0?location.X():
            axis==1?location.Y():location.Z();
        const double component=axis==0?direction.X():
            axis==1?direction.Y():direction.Z();
        if(std::abs(coordinate-value)<=1.0e-9&&
           std::abs(std::abs(component)-1.)<=1.0e-12)return face;
    }
    throw std::runtime_error("Planar fixture face was not found");
}

zima::kernel::FaceReference face_reference_for(
        const OcctBoundarySolid& solid,const TopoDS_Face& face) {
    const auto found=std::ranges::find_if(solid.faces,[&](const auto& value) {
        return value.shape.IsSame(face);
    });
    if(found==solid.faces.end()||found->references.empty())
        throw std::runtime_error("Fixture face has no stable reference");
    return found->references.front();
}

void verify_current_intersection_resolution() {
    constexpr double tolerance=1.0e-7;
    const auto first_shape=BRepPrimAPI_MakeBox(
        gp_Pnt(0,0,0),10,10,10).Shape();
    const auto second_shape=BRepPrimAPI_MakeBox(
        gp_Pnt(5,2,-5),10,6,20).Shape();
    const auto first=input(first_shape,"first",1);
    const auto second=input(second_shape,"second",2);
    std::vector<OcctBoundaryFace> source_faces=first.faces;
    source_faces.insert(source_faces.end(),second.faces.begin(),
        second.faces.end());

    const auto first_x=face_reference_for(first,
        planar_face_at(first_shape,0,10));
    const auto second_y=face_reference_for(second,
        planar_face_at(second_shape,1,2));
    const auto first_z0=face_reference_for(first,
        planar_face_at(first_shape,2,0));
    const auto first_z10=face_reference_for(first,
        planar_face_at(first_shape,2,10));
    const std::set<std::string> parents{
        encoded_reference(first_x),encoded_reference(second_y)};
    const std::set<std::string> first_endpoint{
        encoded_reference(first_x),encoded_reference(second_y),
        encoded_reference(first_z0)};
    const std::set<std::string> second_endpoint{
        encoded_reference(first_x),encoded_reference(second_y),
        encoded_reference(first_z10)};
    const std::set<std::string> endpoints{
        encoded_set(first_endpoint),encoded_set(second_endpoint)};
    const zima::kernel::EdgeReference requested{"second",
        "boolean:add:intersection:between:"+encoded_set(parents)+
            ":ends:"+encoded_set(endpoints),{}};

    BRepAlgoAPI_Fuse fuse(first_shape,second_shape);
    fuse.SetRunParallel(true);fuse.SetFuzzyValue(tolerance);fuse.Build();
    require(fuse.IsDone()&&!fuse.Shape().IsNull()&&
            BRepCheck_Analyzer(fuse.Shape()).IsValid(),
        "Intersection resolver fixture Fuse failed");
    const auto raw=zima::kernel::occt_resolve_add_intersection_edges(
        fuse.Shape(),source_faces,std::span(&requested,1),tolerance);
    require(raw.size()==1&&raw.front().reference==requested,
        "Current Boolean intersection did not resolve from its parents");

    ShapeUpgrade_UnifySameDomain unify(fuse.Shape(),true,true,false);
    unify.SetLinearTolerance(tolerance);unify.Build();
    const auto unified=zima::kernel::occt_resolve_add_intersection_edges(
        unify.Shape(),source_faces,std::span(&requested,1),tolerance);
    require(unified.size()==1&&unified.front().reference==requested,
        "Unified Boolean intersection lost its parent resolution");
    std::ranges::reverse(source_faces);
    const auto reversed=zima::kernel::occt_resolve_add_intersection_edges(
        unify.Shape(),source_faces,std::span(&requested,1),tolerance);
    require(reversed.size()==1&&reversed.front().shape.IsSame(
            unified.front().shape),
        "Boolean intersection resolution depends on source traversal order");

    const auto unavailable=face_reference_for(first,
        planar_face_at(first_shape,0,0));
    const std::set<std::string> wrong_parents{
        encoded_reference(unavailable),encoded_reference(second_y)};
    const zima::kernel::EdgeReference wrong{"second",
        "boolean:add:intersection:between:"+encoded_set(wrong_parents)+
            ":ends:",{}};
    bool rejected=false;
    try {
        static_cast<void>(zima::kernel::occt_resolve_add_intersection_edges(
            unify.Shape(),source_faces,std::span(&wrong,1),tolerance));
    } catch(const std::runtime_error&) {rejected=true;}
    require(rejected,
        "Boolean intersection resolver selected a nearby unrelated edge");
}

void verify_current_treatment_edge_resolution() {
    constexpr double tolerance=1.0e-7;
    const std::vector<OcctBoundarySolid> inputs{
        input(BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
            "first",1),
        input(BRepPrimAPI_MakeBox(gp_Pnt(4,3,-2),8,9,14).Shape(),
            "second",2)};
    const auto boundary=zima::kernel::occt_boundary_union(
        inputs,tolerance);
    std::vector<OcctBoundaryFace> source_faces;
    source_faces.reserve(boundary.faces.size());
    for(const auto& face:boundary.faces)
        source_faces.push_back(
            {face.shape,face.references,face.history_index});

    TopTools_IndexedDataMapOfShapeListOfShape edge_faces,vertex_faces;
    TopExp::MapShapesAndAncestors(boundary.shape,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    TopExp::MapShapesAndAncestors(boundary.shape,TopAbs_VERTEX,TopAbs_FACE,
        vertex_faces);
    const auto alias_for=[&](const TopoDS_Shape& face) {
        const auto found=std::ranges::find_if(source_faces,
            [&](const auto& value) {return value.shape.IsSame(face);});
        if(found==source_faces.end()||found->references.empty())
            throw std::runtime_error(
                "Treatment resolver fixture face has no ancestry");
        return found->references.back();
    };

    TopoDS_Edge selected;
    std::set<std::string> adjacent;
    std::set<std::string> encoded_endpoints;
    for(int index=1;index<=edge_faces.Extent()&&selected.IsNull();++index) {
        const auto& faces=edge_faces.FindFromIndex(index);
        if(faces.Extent()!=2)continue;
        std::set<std::string> candidate_adjacent;
        for(TopTools_ListIteratorOfListOfShape face(faces);face.More();
                face.Next())
            candidate_adjacent.insert(encoded_reference(
                alias_for(face.Value())));
        if(candidate_adjacent.size()!=2)continue;
        std::set<std::string> candidate_endpoints;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge_faces.FindKey(index),TopAbs_VERTEX,vertices);
        if(vertices.Extent()!=2)continue;
        for(int vertex=1;vertex<=vertices.Extent();++vertex) {
            const int support_index=vertex_faces.FindIndex(
                vertices(vertex));
            if(!support_index)continue;
            std::set<std::string> support;
            for(TopTools_ListIteratorOfListOfShape face(
                    vertex_faces.FindFromIndex(support_index));face.More();
                    face.Next())
                support.insert(encoded_reference(alias_for(face.Value())));
            if(!support.empty())candidate_endpoints.insert(
                encoded_set(support));
        }
        if(candidate_endpoints.size()!=2)continue;
        selected=TopoDS::Edge(edge_faces.FindKey(index));
        adjacent=std::move(candidate_adjacent);
        encoded_endpoints=std::move(candidate_endpoints);
    }
    require(!selected.IsNull(),
        "Treatment resolver fixture has no manifold bounded edge");
    const std::set<std::string> selected_parent{
        encoded_reference(zima::kernel::EdgeReference{
            "seed","edge",{}})};
    const zima::kernel::EdgeReference requested{
        "round","fillet:edge:from:"+encoded_set(selected_parent)+
            ":between:"+encoded_set(adjacent)+":ends:"+
            encoded_set(encoded_endpoints),{}};
    const auto resolved=zima::kernel::occt_resolve_edge_treatment_edges(
        boundary.shape,source_faces,std::span(&requested,1),tolerance);
    require(resolved.size()==1&&resolved.front().shape.IsSame(selected)&&
            resolved.front().reference==requested,
        "Treatment edge did not resolve from face and endpoint ancestry");
    std::ranges::reverse(source_faces);
    const auto reversed=zima::kernel::occt_resolve_edge_treatment_edges(
        boundary.shape,source_faces,std::span(&requested,1),tolerance);
    require(reversed.size()==1&&reversed.front().shape.IsSame(selected),
        "Treatment edge resolution depends on face traversal order");
}

void verify_case(const char* name,const TopoDS_Shape& first_shape,
        const TopoDS_Shape& second_shape,bool expect_merged_parent=false) {
    constexpr double tolerance=1.0e-7;
    const std::vector<OcctBoundarySolid> inputs{
        input(first_shape,"oldest",1),input(second_shape,"tool",2)};
    const auto result=zima::kernel::occt_boundary_union(inputs,tolerance);
    const std::vector<std::string> history_owners{
        "unused","oldest","tool"};
    const auto named=zima::kernel::occt_boundary_named_topology(
        result,history_owners,tolerance);
    const auto reference_set=[](const auto& entries) {
        std::set<std::string> references;
        for(const auto& entry:entries)
            references.insert(entry.reference.owner_id+'\x1f'+
                entry.reference.semantic_key+'\x1f'+
                entry.reference.instance_path);
        return references;
    };
    require(named.faces.size()==result.faces.size()&&
            named.edges.size()==result.edges.size()&&
            named.vertices.size()==result.vertices.size()&&
            reference_set(named.faces).size()==named.faces.size()&&
            reference_set(named.edges).size()==named.edges.size()&&
            reference_set(named.vertices).size()==named.vertices.size(),
        "Boundary union did not create one canonical identity per object");
    const auto repeated=zima::kernel::occt_boundary_named_topology(
        result,history_owners,tolerance);
    require(reference_set(named.faces)==reference_set(repeated.faces)&&
            reference_set(named.edges)==reference_set(repeated.edges)&&
            reference_set(named.vertices)==reference_set(repeated.vertices),
        "Boundary union identities changed without changing their inputs");
    const std::vector<OcctBoundarySolid> reversed_inputs{
        input(second_shape,"tool",2),input(first_shape,"oldest",1)};
    const auto reversed_union=zima::kernel::occt_boundary_union(
        reversed_inputs,tolerance);
    const auto reversed=zima::kernel::occt_boundary_named_topology(
        reversed_union,history_owners,tolerance);
    require(reference_set(named.faces)==reference_set(reversed.faces)&&
            reference_set(named.edges)==reference_set(reversed.edges)&&
            reference_set(named.vertices)==reference_set(reversed.vertices),
        "Boundary union identities depend on operand traversal order");
    require(!result.shape.IsNull()&&BRepCheck_Analyzer(result.shape).IsValid(),
        "Boundary union returned an invalid shape");
    TopTools_IndexedMapOfShape actual_faces,actual_edges,actual_vertices;
    TopExp::MapShapes(result.shape,TopAbs_FACE,actual_faces);
    TopExp::MapShapes(result.shape,TopAbs_EDGE,actual_edges);
    TopExp::MapShapes(result.shape,TopAbs_VERTEX,actual_vertices);
    require(actual_faces.Extent()==static_cast<int>(result.faces.size()),
        "Boundary union omitted final face provenance");
    require(actual_edges.Extent()==static_cast<int>(result.edges.size())&&
            actual_vertices.Extent()==static_cast<int>(result.vertices.size()),
        "Boundary union omitted final curve or point provenance");
    require(std::ranges::all_of(result.faces,[&](const auto& face) {
        return actual_faces.Contains(face.shape)&&!face.references.empty()&&
            face.references.front().valid()&&
            face.references.front().instance_path.empty();
    }),"Boundary union returned incomplete face provenance");
    require(std::ranges::all_of(result.edges,[&](const auto& edge) {
        return actual_edges.Contains(edge.shape)&&
            std::ranges::all_of(edge.references,[](const auto& reference) {
                return reference.valid()&&reference.instance_path.empty();
            });
    })&&std::ranges::all_of(result.vertices,[&](const auto& vertex) {
        return actual_vertices.Contains(vertex.shape)&&
            std::ranges::all_of(vertex.references,[](const auto& reference) {
                return reference.valid()&&reference.instance_path.empty();
            });
    }),"Boundary union returned invalid curve or point parents");
    require(std::ranges::any_of(result.edges,[](const auto& edge) {
        return !edge.references.empty();
    })&&std::ranges::any_of(result.vertices,[](const auto& vertex) {
        return !vertex.references.empty();
    }),"Boundary union lost every authored curve or point parent");
    if(expect_merged_parent)
        require(std::ranges::any_of(result.faces,[](const auto& face) {
            return face.references.size()>2&&
                face.references.front().owner_id=="oldest";
        }),"Coplanar union did not retain oldest-parent aliases");

    BRepAlgoAPI_Fuse reference(first_shape,second_shape);
    reference.SetRunParallel(true);
    reference.SetFuzzyValue(tolerance);
    reference.Build();
    require(reference.IsDone()&&!reference.Shape().IsNull()&&
            BRepCheck_Analyzer(reference.Shape()).IsValid(),
        "Reference fuse failed");
    const double missing=material_difference(
        reference.Shape(),result.shape,tolerance);
    const double excess=material_difference(
        result.shape,reference.Shape(),tolerance);
    if(missing>1.0e-8||excess>1.0e-8)
        std::cerr<<name<<" missing="<<missing<<" excess="<<excess
            <<" graph_ms="<<result.metrics.graph_ms
            <<" split_ms="<<result.metrics.split_ms
            <<" classify_ms="<<result.metrics.classify_ms
            <<" sew_ms="<<result.metrics.sew_ms
            <<" unify_ms="<<result.metrics.unify_ms<<'\n';
    require(missing<=1.0e-8&&excess<=1.0e-8,
        "Boundary union changed material");
}

std::array<int,3> topology_counts(const TopoDS_Shape& shape) {
    std::array<int,3> result{};
    TopTools_IndexedMapOfShape members;
    TopExp::MapShapes(shape,TopAbs_FACE,members);result[0]=members.Extent();
    members.Clear();TopExp::MapShapes(shape,TopAbs_EDGE,members);
    result[1]=members.Extent();
    members.Clear();TopExp::MapShapes(shape,TopAbs_VERTEX,members);
    result[2]=members.Extent();
    return result;
}

void verify_composition_case(const char* name,
        const std::vector<TopoDS_Shape>& positive_shapes,
        const std::vector<TopoDS_Shape>& negative_shapes) {
    constexpr double tolerance=1.0e-7;
    require(!positive_shapes.empty()&&!negative_shapes.empty(),
        "Signed-composition fixture is incomplete");
    std::vector<OcctBoundaryRegion> regions;
    std::size_t history_index{};
    for(std::size_t index=0;index<positive_shapes.size();++index)
        regions.push_back({input(positive_shapes[index],
            "positive:"+std::to_string(index),history_index++),true});
    for(std::size_t index=0;index<negative_shapes.size();++index)
        regions.push_back({input(negative_shapes[index],
            "negative:"+std::to_string(index),history_index++),false});

    const auto result=zima::kernel::occt_boundary_composition(
        regions,tolerance);
    require(!result.shape.IsNull()&&BRepCheck_Analyzer(result.shape).IsValid(),
        "Boundary composition returned an invalid shape");
    require(std::ranges::all_of(result.faces,[](const auto& face) {
        return !face.references.empty()&&face.references.front().valid();
    }),"Boundary composition lost face provenance");
    require(std::ranges::any_of(result.faces,[](const auto& face) {
        return std::ranges::any_of(face.references,[](const auto& reference) {
            return reference.owner_id.starts_with("negative:");
        });
    }),"Boundary composition lost the subtractive boundary owner");
    std::vector<std::string> history_owners;
    for(std::size_t index=0;index<positive_shapes.size();++index)
        history_owners.push_back("positive:"+std::to_string(index));
    for(std::size_t index=0;index<negative_shapes.size();++index)
        history_owners.push_back("negative:"+std::to_string(index));
    const auto named=
        zima::kernel::occt_boundary_composition_named_topology(
            result,history_owners,tolerance);
    const auto identities=[](const auto& topology) {
        std::set<std::string> values;
        const auto append=[&](char kind,const auto& entries) {
            for(const auto& entry:entries)
                values.insert(std::string(1,kind)+':'+
                    encoded_reference(entry.reference));
        };
        append('f',topology.faces);append('e',topology.edges);
        append('v',topology.vertices);
        return values;
    };
    const auto named_identities=identities(named);
    require(named.faces.size()==result.faces.size()&&
            named.edges.size()==result.edges.size()&&
            named.vertices.size()==result.vertices.size()&&
            named_identities.size()==named.faces.size()+named.edges.size()+
                named.vertices.size(),
        "Boundary composition did not create canonical topology identities");
    require(std::ranges::any_of(named.edges,[](const auto& edge) {
        return edge.reference.semantic_key.starts_with(
            "boundary-composition:signed:");
    }),"Boundary composition reused the Add identity namespace");
    const auto repeated=
        zima::kernel::occt_boundary_composition_named_topology(
            result,history_owners,tolerance);
    require(identities(repeated)==named_identities,
        "Boundary composition identities changed without input changes");

    TopoDS_Shape reference=positive_shapes.front();
    for(std::size_t index=1;index<positive_shapes.size();++index) {
        BRepAlgoAPI_Fuse fuse(reference,positive_shapes[index]);
        fuse.SetRunParallel(true);fuse.SetFuzzyValue(tolerance);fuse.Build();
        require(fuse.IsDone()&&!fuse.Shape().IsNull(),
            "Signed-composition reference Fuse failed");
        reference=fuse.Shape();
    }
    for(const auto& negative:negative_shapes) {
        BRepAlgoAPI_Cut cut(reference,negative);
        cut.SetRunParallel(true);cut.SetFuzzyValue(tolerance);cut.Build();
        require(cut.IsDone()&&!cut.Shape().IsNull(),
            "Signed-composition reference Cut failed");
        reference=cut.Shape();
    }
    ShapeUpgrade_UnifySameDomain reference_unifier(
        reference,true,true,false);
    reference_unifier.SetLinearTolerance(tolerance);
    reference_unifier.Build();
    reference=reference_unifier.Shape();
    require(!reference.IsNull()&&BRepCheck_Analyzer(reference).IsValid(),
        "Signed-composition reference is invalid");

    const double missing=material_difference(
        reference,result.shape,tolerance);
    const double excess=material_difference(
        result.shape,reference,tolerance);
    const auto expected_counts=topology_counts(reference);
    const auto actual_counts=topology_counts(result.shape);
    if(missing>1.0e-8||excess>1.0e-8||
       expected_counts!=actual_counts)
        std::cerr<<name<<" missing="<<missing<<" excess="<<excess
            <<" expected="<<expected_counts[0]<<','<<expected_counts[1]
            <<','<<expected_counts[2]<<" actual="<<actual_counts[0]<<','
            <<actual_counts[1]<<','<<actual_counts[2]
            <<" graph_ms="<<result.metrics.graph_ms
            <<" split_ms="<<result.metrics.split_ms
            <<" classify_ms="<<result.metrics.classify_ms
            <<" sew_ms="<<result.metrics.sew_ms
            <<" unify_ms="<<result.metrics.unify_ms<<'\n';
    require(missing<=1.0e-8&&excess<=1.0e-8,
        "Boundary composition changed material");
    require(expected_counts==actual_counts,
        "Boundary composition changed exact topology counts");

    std::ranges::reverse(regions);
    const auto reversed=zima::kernel::occt_boundary_composition(
        regions,tolerance);
    const auto reversed_named=
        zima::kernel::occt_boundary_composition_named_topology(
            reversed,history_owners,tolerance);
    require(material_difference(result.shape,reversed.shape,tolerance)<=1.0e-8&&
            material_difference(reversed.shape,result.shape,tolerance)<=1.0e-8&&
            topology_counts(reversed.shape)==actual_counts&&
            identities(reversed_named)==named_identities,
        "Boundary composition depends on region traversal order");
}

} // namespace

int main() try {
    verify_composition_case("box through opening",
        {BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),20,16,12).Shape()},
        {BRepPrimAPI_MakeBox(gp_Pnt(5,4,-2),8,6,16).Shape()});
    verify_composition_case("curved cylindrical opening",
        {BRepPrimAPI_MakeBox(gp_Pnt(-8,-8,0),16,16,12).Shape()},
        {BRepPrimAPI_MakeCylinder(3,16).Shape()});
    verify_composition_case("two positive regions and shared removal",
        {BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),12,12,10).Shape(),
         BRepPrimAPI_MakeBox(gp_Pnt(8,2,1),10,8,8).Shape()},
        {BRepPrimAPI_MakeBox(gp_Pnt(9,4,-1),4,4,12).Shape()});
    verify_current_intersection_resolution();
    verify_current_treatment_edge_resolution();
    verify_case("crossing boxes",
        BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(4,3,-2),8,9,14).Shape());
    verify_case("coplanar boxes",
        BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(4,0,0),8,10,10).Shape(),true);
    verify_case("cylinder and box",
        BRepPrimAPI_MakeCylinder(4,14).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-5,-5,2),10,10,6).Shape());
    verify_case("disjoint boxes",
        BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),4,5,6).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),3,4,5).Shape());
    verify_case("multi-solid authored operand",
        compound({
            BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(6,2,1),8,6,7).Shape()}),
        BRepPrimAPI_MakeBox(gp_Pnt(30,0,0),3,4,5).Shape());

    const auto outer=BRepPrimAPI_MakeBox(
        gp_Pnt(0,0,0),10,10,4).Shape();
    const auto opening=BRepPrimAPI_MakeBox(
        gp_Pnt(2,2,-1),6,6,6).Shape();
    BRepAlgoAPI_Cut frame_builder(outer,opening);
    frame_builder.Build();
    require(frame_builder.IsDone()&&
            BRepCheck_Analyzer(frame_builder.Shape()).IsValid(),
        "Thin-frame fixture failed");
    verify_case("thin-frame boundary contact",frame_builder.Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(-1,4,1),12,2,2).Shape());

    const auto cavity_outer=BRepPrimAPI_MakeBox(
        gp_Pnt(0,0,0),20,20,20).Shape();
    const auto cavity_void=BRepPrimAPI_MakeBox(
        gp_Pnt(4,4,4),12,12,12).Shape();
    BRepAlgoAPI_Cut cavity_builder(cavity_outer,cavity_void);
    cavity_builder.Build();
    require(cavity_builder.IsDone()&&
            BRepCheck_Analyzer(cavity_builder.Shape()).IsValid(),
        "Enclosed-cavity fixture failed");
    verify_case("enclosed cavity",cavity_builder.Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(18,6,6),6,8,8).Shape());

    std::cout<<"OCCT boundary union/composition: signed material, "
        "current parent/treatment-edge resolution, "
        "exact material, valid shell, "
        "coplanar oldest-parent, curved, disjoint, multi-solid, thin-frame "
        "and enclosed-cavity checks passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
