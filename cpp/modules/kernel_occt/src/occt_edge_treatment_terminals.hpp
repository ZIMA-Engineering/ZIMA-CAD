#pragma once

#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
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
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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

inline std::vector<TopoDS_Face> nearby_planar_terminal_faces(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,
        std::span<const TopoDS_Face> curved_terminals,
        const gp_Pnt& endpoint,const gp_Dir& outward,
        double profile_reach,double tolerance) {
    if(curved_terminals.empty()||profile_reach<=tolerance)return {};
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(material,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    const auto same_plane=[&](const TopoDS_Face& first,
            const TopoDS_Face& second) {
        const BRepAdaptor_Surface a(first,true),b(second,true);
        if(a.GetType()!=GeomAbs_Plane||b.GetType()!=GeomAbs_Plane)
            return false;
        const auto first_plane=a.Plane(),second_plane=b.Plane();
        return first_plane.Axis().Direction().IsParallel(
                   second_plane.Axis().Direction(),1.0e-9)&&
            first_plane.Distance(second_plane.Location())<=tolerance;
    };
    std::vector<TopoDS_Face> result;
    const auto append=[&](const TopoDS_Face& face) {
        if(std::ranges::none_of(result,[&](const auto& current) {
                return current.IsSame(face)||same_plane(current,face);
            }))result.push_back(face);
    };
    for(const auto& curved:curved_terminals)
        for(TopExp_Explorer edge_item(curved,TopAbs_EDGE);
                edge_item.More();edge_item.Next()) {
            const int edge_index=edge_faces.FindIndex(edge_item.Current());
            if(!edge_index)continue;
            for(const auto& adjacent_shape:
                    edge_faces.FindFromIndex(edge_index)) {
                const auto adjacent=TopoDS::Face(adjacent_shape);
                if(adjacent.IsSame(curved)||
                   std::ranges::any_of(curved_terminals,
                       [&](const auto& current) {
                           return adjacent.IsSame(current);
                       })||
                   adjacent.IsSame(first_support)||
                   adjacent.IsSame(second_support))continue;
                const BRepAdaptor_Surface support(adjacent,true);
                if(support.GetType()!=GeomAbs_Plane)continue;
                const auto plane=support.Plane();
                const auto normal=plane.Axis().Direction();
                const double incidence=normal.Dot(outward);
                if(std::abs(incidence)<=1.0e-8)continue;
                const double axial_distance=gp_Vec(endpoint,
                    plane.Location()).Dot(gp_Vec(normal))/incidence;
                // Faces through the exact endpoint are already classified by
                // local_terminal_faces/automatic_terminal_faces.  This search
                // adds only the nearby boundary of an incident Fillet or
                // Chamfer behind the current endpoint. Its reach is owned by
                // the new treatment size; equality is a tangential,
                // zero-volume contact rather than another trimming plane.
                if(axial_distance>=-tolerance||
                   -axial_distance>=profile_reach-tolerance||
                   same_plane(adjacent,first_support)||
                   same_plane(adjacent,second_support))continue;
                append(adjacent);
            }
        }
    return result;
}

inline std::vector<TopoDS_Face> nearby_nonplanar_terminal_faces(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,
        std::span<const TopoDS_Face> curved_terminals,
        const gp_Pnt& endpoint,const gp_Dir& outward,
        double profile_reach,double tolerance) {
    if(curved_terminals.empty()||profile_reach<=tolerance)return {};
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(material,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    std::vector<TopoDS_Face> result;
    const auto append=[&](const TopoDS_Face& face) {
        if(std::ranges::none_of(result,[&](const auto& current) {
                return current.IsSame(face)||
                    BRep_Tool::Surface(current)==BRep_Tool::Surface(face);
            }))result.push_back(face);
    };
    for(const auto& curved:curved_terminals)
        for(TopExp_Explorer edge_item(curved,TopAbs_EDGE);
                edge_item.More();edge_item.Next()) {
            const auto shared_edge=TopoDS::Edge(edge_item.Current());
            const int edge_index=edge_faces.FindIndex(shared_edge);
            if(!edge_index)continue;
            for(const auto& adjacent_shape:
                    edge_faces.FindFromIndex(edge_index)) {
                const auto adjacent=TopoDS::Face(adjacent_shape);
                if(adjacent.IsSame(curved)||
                   std::ranges::any_of(curved_terminals,
                       [&](const auto& current) {
                           return adjacent.IsSame(current)||
                               BRep_Tool::Surface(adjacent)==
                                   BRep_Tool::Surface(current);
                       })||
                   adjacent.IsSame(first_support)||
                   adjacent.IsSame(second_support)||
                   BRepAdaptor_Surface(adjacent,true).GetType()==
                       GeomAbs_Plane)continue;
                const BRepAdaptor_Curve boundary(shared_edge);
                const double first=boundary.FirstParameter();
                const double last=boundary.LastParameter();
                bool within_reach{};
                for(const double ratio:{0.,.25,.5,.75,1.}) {
                    const auto point=boundary.Value(
                        first+(last-first)*ratio);
                    const double inward=-gp_Vec(endpoint,point).Dot(
                        gp_Vec(outward));
                    if(inward>tolerance&&
                       inward<profile_reach-tolerance&&
                       endpoint.Distance(point)<=
                           2.*profile_reach+tolerance) {
                        within_reach=true;
                        break;
                    }
                }
                if(within_reach)append(adjacent);
            }
        }
    return result;
}

inline std::optional<double> forward_terminal_intersection_distance(
        const TopoDS_Face& face,const gp_Pnt& endpoint,
        const gp_Dir& outward,double tolerance) {
    if(face.IsNull())return std::nullopt;
    const BRepAdaptor_Surface support(face,true);
    if(support.GetType()==GeomAbs_Plane) {
        const auto plane=support.Plane();
        const auto normal=plane.Axis().Direction();
        const double incidence=normal.Dot(outward);
        if(std::abs(incidence)<=1.0e-8)return std::nullopt;
        const double distance=gp_Vec(endpoint,plane.Location()).Dot(
            gp_Vec(normal))/incidence;
        return distance>tolerance?std::optional<double>{distance}:
            std::nullopt;
    }
    IntCurvesFace_ShapeIntersector hit;
    hit.Load(face,tolerance);
    hit.Perform(gp_Lin(endpoint,outward),0.,1.0e100);
    if(!hit.IsDone())return std::nullopt;
    double distance=std::numeric_limits<double>::infinity();
    for(int index=1;index<=hit.NbPnt();++index) {
        const double parameter=hit.WParameter(index);
        if(parameter>tolerance)distance=std::min(distance,parameter);
    }
    return std::isfinite(distance)?std::optional<double>{distance}:
        std::nullopt;
}

inline std::vector<std::vector<TopoDS_Face>>
remote_terminal_face_choices_through_local_curve(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& outward,double tolerance,
        bool adjacent_planar_only=false) {
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(material,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    struct Candidate {
        TopoDS_Face face;
        std::optional<gp_Pln> plane;
        double distance{};
        int support_adjacency{};
    };
    std::vector<Candidate> candidates;
    const gp_Lin route_ray(endpoint,outward);
    const auto append_candidate=[&](const TopoDS_Face& adjacent,
            int adjacency) {
        if(adjacent.IsNull()||adjacent.IsSame(first_support)||
           adjacent.IsSame(second_support))return;
        const BRepAdaptor_Surface support(adjacent,true);
        std::optional<gp_Pln> plane;
        double distance=std::numeric_limits<double>::infinity();
        if(support.GetType()==GeomAbs_Plane) {
            plane=support.Plane();
            const auto normal=plane->Axis().Direction();
            const double incidence=normal.Dot(outward);
            if(std::abs(incidence)<=1.0e-8)return;
            distance=gp_Vec(endpoint,plane->Location()).Dot(
                gp_Vec(normal))/incidence;
            if(distance<=tolerance)return;
            const gp_Pnt intersection=endpoint.Translated(
                gp_Vec(outward)*distance);
            BRepClass_FaceClassifier at_intersection(adjacent,
                intersection,10.*tolerance);
            const bool bounded_hit=at_intersection.State()==TopAbs_IN||
                at_intersection.State()==TopAbs_ON;
            if(!bounded_hit&&adjacency==0)return;
            adjacency+=bounded_hit?1:0;
            if(adjacent_planar_only) {
                adjacency=bounded_hit?1:0;
                for(const auto& face:{first_support,second_support})
                    if(terminal_face_meets_support_at_endpoint(adjacent,
                            face,intersection,10.*tolerance))++adjacency;
            }
        } else {
            if(adjacent_planar_only)return;
            IntCurvesFace_ShapeIntersector hit;
            hit.Load(adjacent,tolerance);
            hit.Perform(route_ray,0.,1.0e100);
            if(!hit.IsDone())return;
            for(int index=1;index<=hit.NbPnt();++index) {
                const double parameter=hit.WParameter(index);
                if(parameter>tolerance)
                    distance=std::min(distance,parameter);
            }
            if(!std::isfinite(distance))return;
            ++adjacency;
        }
        const auto duplicate=std::ranges::find_if(candidates,
            [&](const auto& candidate) {
                return candidate.face.IsSame(adjacent);
            });
        if(duplicate==candidates.end())
            candidates.push_back({adjacent,plane,distance,adjacency});
        else {
            duplicate->distance=std::min(duplicate->distance,distance);
            duplicate->support_adjacency=std::max(
                duplicate->support_adjacency,adjacency);
        }
    };
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
                const int adjacency=
                    (terminal_face_meets_support_at_endpoint(adjacent,
                        first_support,endpoint,10.*tolerance)?1:0)+
                    (terminal_face_meets_support_at_endpoint(adjacent,
                        second_support,endpoint,10.*tolerance)?1:0);
                // Topological adjacency remains authoritative when a
                // preceding radius trims away the centre-line intersection.
                append_candidate(adjacent,adjacency+1);
            }
        }
    }
    // Follow the complete outward ray. This includes planar, circular and
    // spline boundaries beyond the first local curve. The caller validates
    // every candidate by calculating the complete private result.
    if(!adjacent_planar_only)
        for(TopExp_Explorer item(material,TopAbs_FACE);item.More();item.Next()) {
            const auto face=TopoDS::Face(item.Current());
            if(std::ranges::any_of(local_curves,[&](const auto& local) {
                    return local.IsSame(face);
                }))continue;
            append_candidate(face,0);
        }
    std::ranges::stable_sort(candidates,[adjacent_planar_only](
            const auto& first,const auto& second) {
        if(adjacent_planar_only&&
           first.support_adjacency!=second.support_adjacency)
            return first.support_adjacency>second.support_adjacency;
        if(first.distance!=second.distance)
            return first.distance<second.distance;
        if(first.support_adjacency!=second.support_adjacency)
            return first.support_adjacency>second.support_adjacency;
        return false;
    });
    std::vector<std::vector<TopoDS_Face>> result;
    for(const auto& candidate:candidates) {
        const auto group=std::ranges::find_if(result,[&](const auto& faces) {
            if(faces.empty())return false;
            const auto existing=std::ranges::find_if(candidates,
                [&](const auto& current) {
                    return current.face.IsSame(faces.front());
                });
            if(existing==candidates.end())return false;
            if(adjacent_planar_only&&
               candidate.support_adjacency!=existing->support_adjacency)
                return false;
            if(candidate.plane&&existing->plane)
                return candidate.plane->Axis().Direction().IsParallel(
                           existing->plane->Axis().Direction(),1.0e-9)&&
                    candidate.plane->Distance(
                        existing->plane->Location())<=tolerance;
            return !candidate.plane&&!existing->plane&&
                BRep_Tool::Surface(candidate.face)==
                    BRep_Tool::Surface(existing->face)&&
                std::abs(candidate.distance-existing->distance)<=
                    10.*tolerance;
        });
        if(group==result.end())result.push_back({candidate.face});
        else group->push_back(candidate.face);
    }
    return result;
}

inline std::vector<TopoDS_Face> selected_terminal_faces(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& outward,const TopoDS_Face& selected,
        double tolerance) {
    if(selected.IsNull())return {};
    auto local=local_terminal_faces(material,first_support,second_support,
        endpoint,tolerance,false);
    if(std::ranges::any_of(local,[&](const auto& face) {
            return face.IsSame(selected);
        }))return local;
    auto remote=remote_terminal_face_choices_through_local_curve(material,
        first_support,second_support,endpoint,outward,tolerance);
    const auto choice=std::ranges::find_if(remote,[&](const auto& faces) {
        return std::ranges::any_of(faces,[&](const auto& face) {
            return face.IsSame(selected);
        });
    });
    return choice==remote.end()?std::vector<TopoDS_Face>{selected}:
        std::move(*choice);
}

inline std::vector<TopoDS_Face> remote_terminal_faces_through_local_curve(
        const TopoDS_Solid& material,const TopoDS_Face& first_support,
        const TopoDS_Face& second_support,const gp_Pnt& endpoint,
        const gp_Dir& outward,double tolerance) {
    auto choices=remote_terminal_face_choices_through_local_curve(material,
        first_support,second_support,endpoint,outward,tolerance,true);
    return choices.empty()?std::vector<TopoDS_Face>{}:
        std::move(choices.front());
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

inline bool tangent_terminal_leaves_material(const TopoDS_Solid& material,
        std::span<const TopoDS_Face> faces,const gp_Pnt& endpoint,
        const gp_Dir& outward,double profile_size,double tolerance) {
    if(faces.empty()||route_crosses_curved_terminal(faces,endpoint,outward,tolerance))
        return false;
    // A tangent continuation into empty space has no material support for
    // an additive tail. A crossed envelope still owns its exact intersection;
    // an earlier inner radius (material ahead) still trims the new profile.
    const auto probe=endpoint.Translated(gp_Vec(outward)*
        std::max(100.*tolerance,.02*profile_size));
    return BRepClass3d_SolidClassifier(material,probe,tolerance).State()==TopAbs_OUT;
}

} // namespace zima::kernel::edge_treatment_terminals
