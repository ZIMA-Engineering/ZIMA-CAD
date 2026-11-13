#pragma once

#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <vector>

namespace zima::kernel::edge_treatment_terminals {

inline bool terminal_face_meets_support_at_endpoint(
        const TopoDS_Face& candidate,const TopoDS_Face& support,
        const gp_Pnt& endpoint,double tolerance) {
    TopTools_IndexedMapOfShape support_edges;
    TopExp::MapShapes(support,TopAbs_EDGE,support_edges);
    for(TopExp_Explorer item(candidate,TopAbs_EDGE);item.More();item.Next()) {
        const auto edge=TopoDS::Edge(item.Current());
        if(!support_edges.Contains(edge))continue;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        for(int index=1;index<=vertices.Extent();++index)
            if(BRep_Tool::Pnt(TopoDS::Vertex(
                    vertices.FindKey(index))).Distance(endpoint)<=
                    tolerance)return true;
    }
    return false;
}

inline std::vector<TopoDS_Face> local_terminal_faces(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        double tolerance,bool curved_only) {
    struct Candidate {
        TopoDS_Face face;
        bool meets_first{};
        bool meets_second{};
    };
    std::vector<Candidate> candidates;
    for(TopExp_Explorer item(material,TopAbs_FACE);item.More();item.Next()) {
        const auto face=TopoDS::Face(item.Current());
        if(face.IsSame(first_support)||face.IsSame(second_support))continue;
        const BRepAdaptor_Surface surface(face,true);
        if(curved_only&&surface.GetType()==GeomAbs_Plane)continue;
        BRepClass_FaceClassifier classifier(face,endpoint,tolerance);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
            continue;
        const bool meets_first=terminal_face_meets_support_at_endpoint(
            face,first_support,endpoint,tolerance);
        const bool meets_second=terminal_face_meets_support_at_endpoint(
            face,second_support,endpoint,tolerance);
        if(meets_first||meets_second)
            candidates.push_back({face,meets_first,meets_second});
    }
    std::vector<TopoDS_Face> result;
    const auto append=[&](const TopoDS_Face& face) {
        if(std::ranges::none_of(result,
                [&](const auto& current){return current.IsSame(face);}))
            result.push_back(face);
    };
    // One face incident to both route supports is the strongest exact end.
    for(const auto& candidate:candidates)
        if(candidate.meets_first&&candidate.meets_second)
            append(candidate.face);
    if(!result.empty())return result;
    // Boolean descendants can split one physical terminal into two faces,
    // one continuing each support. Retain the complete local pair.
    for(std::size_t first=0;first<candidates.size();++first)
        for(std::size_t second=first+1;second<candidates.size();++second) {
            const auto& a=candidates[first];
            const auto& b=candidates[second];
            if(!(a.meets_first||b.meets_first)||
               !(a.meets_second||b.meets_second))continue;
            append(a.face);
            append(b.face);
        }
    return result;
}

inline std::vector<TopoDS_Face> local_curved_terminal_faces(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        double tolerance) {
    return local_terminal_faces(material,first_support,second_support,
        endpoint,tolerance,true);
}

inline std::vector<TopoDS_Face> remote_terminal_faces_through_local_curve(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& outward,double tolerance) {
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(material,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    struct Candidate {
        TopoDS_Face face;
        gp_Pln plane;
        double distance{};
        int support_adjacency{};
    };
    std::vector<Candidate> candidates;
    // Follow the exact local curved terminal already classified for this
    // endpoint. A previous Fillet can be split into multiple Boolean faces,
    // so each adjacent planar cap is considered independently.
    const auto local_curves=local_curved_terminal_faces(material,
        first_support,second_support,endpoint,tolerance);
    for(const auto& curved:local_curves) {
        for(TopExp_Explorer edge_item(curved,TopAbs_EDGE);
                edge_item.More();edge_item.Next()) {
            const auto edge=TopoDS::Edge(edge_item.Current());
            const int edge_index=edge_faces.FindIndex(edge);
            if(!edge_index)continue;
            for(const auto& adjacent_shape:
                    edge_faces.FindFromIndex(edge_index)) {
                const auto adjacent=TopoDS::Face(adjacent_shape);
                if(adjacent.IsSame(curved)||
                   adjacent.IsSame(first_support)||
                   adjacent.IsSame(second_support))continue;
                const BRepAdaptor_Surface support(adjacent,true);
                if(support.GetType()!=GeomAbs_Plane)continue;
                const auto plane=support.Plane();
                const auto normal=plane.Axis().Direction();
                const double incidence=normal.Dot(outward);
                if(std::abs(incidence)<=1.0e-8)continue;
                const double distance=gp_Vec(endpoint,plane.Location()).Dot(
                    gp_Vec(normal))/incidence;
                if(distance<=tolerance)continue;
                const gp_Pnt intersection=endpoint.Translated(
                    gp_Vec(outward)*distance);
                // The preceding radius can trim away the point where the
                // route centre line meets this plane. Topological adjacency
                // is authoritative; an on-face hit strengthens equal choices.
                BRepClass_FaceClassifier at_intersection(adjacent,
                    intersection,10.*tolerance);
                const int adjacency=
                    (terminal_face_meets_support_at_endpoint(adjacent,
                        first_support,intersection,10.*tolerance)?1:0)+
                    (terminal_face_meets_support_at_endpoint(adjacent,
                        second_support,intersection,10.*tolerance)?1:0)+
                    ((at_intersection.State()==TopAbs_IN||
                      at_intersection.State()==TopAbs_ON)?1:0);
                const auto duplicate=std::ranges::find_if(candidates,
                    [&](const auto& candidate) {
                        return candidate.face.IsSame(adjacent);
                    });
                if(duplicate==candidates.end())
                    candidates.push_back(
                        {adjacent,plane,distance,adjacency});
            }
        }
    }
    if(candidates.empty())return {};
    const auto selected=std::ranges::min_element(candidates,
        [](const auto& first,const auto& second) {
            if(first.support_adjacency!=second.support_adjacency)
                return first.support_adjacency>second.support_adjacency;
            return first.distance<second.distance;
        });
    std::vector<TopoDS_Face> result;
    for(const auto& candidate:candidates)
        if(candidate.support_adjacency==selected->support_adjacency&&
           candidate.plane.Axis().Direction().IsParallel(
               selected->plane.Axis().Direction(),1.0e-9)&&
           candidate.plane.Distance(selected->plane.Location())<=tolerance)
            result.push_back(candidate.face);
    return result;
}

inline bool removal_profile_continues_past_curved_terminal(
        const TopoDS_Solid& material,const gp_Pnt& endpoint,
        const gp_Dir& outward,const gp_Dir& first_inward,
        const gp_Dir& second_inward,double profile_size,double tolerance) {
    gp_Vec bisector=gp_Vec(first_inward)+gp_Vec(second_inward);
    if(bisector.SquareMagnitude()<=1.0e-18)return false;
    bisector.Normalize();
    const double axial_probe=std::max(100.*tolerance,profile_size*.02);
    for(const double fraction:{.1,.25}) {
        const gp_Pnt probe=endpoint.Translated(
            gp_Vec(outward)*axial_probe+
            bisector*(profile_size*fraction));
        BRepClass3d_SolidClassifier classifier(material,probe,tolerance);
        if(classifier.State()==TopAbs_IN)return true;
    }
    return false;
}

inline bool route_crosses_curved_terminal(
        std::span<const TopoDS_Face> faces,const gp_Pnt& endpoint,
        const gp_Dir& outward,double tolerance) {
    static_cast<void>(tolerance);
    for(const auto& face:faces) {
        TopLoc_Location location;
        const auto surface=BRep_Tool::Surface(face,location);
        if(surface.IsNull())continue;
        const auto inverse=location.Transformation().Inverted();
        const auto local_endpoint=endpoint.Transformed(inverse);
        GeomAPI_ProjectPointOnSurf projection(local_endpoint,surface);
        // `faces` already contains only topological terminal faces proven to
        // meet the route supports at this endpoint.  Do not reject that exact
        // incidence because the underlying analytical surface carries a
        // larger OCCT construction tolerance than the operation tolerance.
        // Projection is used only to obtain the local differential normal.
        if(projection.NbPoints()==0)continue;
        double u{},v{};
        projection.LowerDistanceParameters(u,v);
        gp_Pnt point;gp_Vec du,dv;
        surface->D1(u,v,point,du,dv);
        du.Transform(location.Transformation());
        dv.Transform(location.Transformation());
        const gp_Vec normal=du.Crossed(dv);
        if(normal.SquareMagnitude()<=1.0e-18)continue;
        // A previous Fillet is tangent to the approaching route and is the
        // exact local stop.  A Body envelope (cylinder, cone, spline, ...)
        // crossed by the route is instead a terminal surface: overgrow the
        // tool so the final operation can establish that exact intersection.
        // This differential test is independent of surface type and radius.
        if(std::abs(gp_Dir(normal).Dot(outward))>1.0e-7)return true;
    }
    return false;
}

} // namespace zima::kernel::edge_treatment_terminals
