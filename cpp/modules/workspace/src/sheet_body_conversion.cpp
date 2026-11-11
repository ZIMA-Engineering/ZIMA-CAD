#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/sheet_rip.hpp>
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <set>

namespace zima::workspace {
namespace {
using namespace kernel::sheet_material;
using V=kernel::Vec3;
using Face=kernel::FaceReference;
constexpr double join_tolerance=.05;
thread_local unsigned sheet_conversion_authoring_depth{};
struct SheetConversionAuthoringScope {
    SheetConversionAuthoringScope(){++sheet_conversion_authoring_depth;}
    ~SheetConversionAuthoringScope(){--sheet_conversion_authoring_depth;}
};
std::vector<kernel::BodyResult> calculate_ripped_source(
        const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,
        const std::string& source_body,
        const std::vector<const sketcher::Sketch*>& rip_sources,
        const kernel::OcctKernel& kernel) {
    if(rip_sources.empty())return {};
    auto operations=source.kernel_operations();
    auto insertion=operations.end();
    for(auto operation=operations.begin();operation!=operations.end();
            ++operation)
        if(operation->body.id==source_body)
            insertion=std::next(operation);
    if(insertion==operations.end()&&
            (operations.empty()||operations.back().body.id!=source_body))
        throw std::invalid_argument(
            "RIP preprocessing could not locate the selected source Body history.");
    const auto source_operation=std::prev(insertion);
    kernel::HistoryOperation rip;
    rip.owner_id="sheet-from-body:rip-analysis:"+source_body;
    rip.primitive=document::sheet_rip_request(rip_sources,join_tolerance);
    rip.boolean_tolerance=source_operation->boolean_tolerance;
    rip.mesh_deflection=source_operation->mesh_deflection;
    rip.body=source_operation->body;
    operations.insert(insertion,std::move(rip));
    auto result=kernel.evaluate_history_incremental(operations,cache);
    if(result.empty())throw std::runtime_error(
        "RIP preprocessing produced no calculated source boundary.");
    return result;
}
std::string key(const Face& face){return face.owner_id+":"+face.semantic_key;}
V normal(const Face& face,V point) {
    const auto& s=*face.surface;auto n=s.axis;
    if(s.kind==kernel::SurfaceGeometry::Kind::Cylinder) {
        const auto delta=sub(point,s.origin);n=unit(sub(delta,mul(s.axis,dot(delta,s.axis))));
    }
    return mul(n,s.reversed?-1:1);
}
bool straight(const kernel::ViewerEdge& edge) {
    if(edge.points.size()<2)return false;
    const auto delta=sub(edge.points.back(),edge.points.front());const double length=std::sqrt(dot(delta,delta));if(length<.001)return false;
    for(auto p:edge.points){const auto off=cross(sub(p,edge.points.front()),delta);if(std::sqrt(dot(off,off))/length>1e-5)return false;}return true;
}
std::map<std::string,Face> faces(const kernel::ViewerMesh& mesh) {
    std::map<std::string,Face> result;for(const auto& face:mesh.triangle_references)result.emplace(key(face),face);
    for(const auto& face:mesh.original_references.triangle_references)if(result.contains(key(face)))result[key(face)]=face;
    return result;
}
bool same_fragment(const Face& first,const Face& second) {
    if(first!=second)return false;
    if(!first.surface||!second.surface)return !first.surface&&!second.surface;
    const auto& a=*first.surface;
    const auto& b=*second.surface;
    if(a.kind!=b.kind)return false;
    const auto squared=[](V value){return dot(value,value);};
    const auto direction_same=[](V x,V y) {
        return std::abs(std::abs(dot(unit(x),unit(y)))-1.)<=1e-8;
    };
    if(a.kind==kernel::SurfaceGeometry::Kind::Plane) {
        const auto an=unit(a.axis);
        const auto bn=unit(b.axis);
        return std::abs(dot(an,bn))>1-1e-8&&
            std::abs(dot(sub(a.origin,b.origin),an))<=1e-6;
    }
    if(a.kind==kernel::SurfaceGeometry::Kind::Cylinder) {
        if(std::abs(a.radius-b.radius)>1e-6||
                !direction_same(a.axis,b.axis))return false;
        return squared(cross(sub(a.origin,b.origin),unit(a.axis)))<=1e-12;
    }
    return a==b;
}
bool same_or_descendant_reference(const Face& candidate,const Face& source) {
    if(candidate.instance_path!=source.instance_path)return false;
    auto owner=candidate.owner_id;
    auto semantic=candidate.semantic_key;
    for(unsigned depth=0;depth<64;++depth) {
        if(owner==source.owner_id&&semantic==source.semantic_key)return true;
        const auto parent=kernel::boolean_face_parent(semantic);
        if(!parent)return false;
        owner=parent->first;
        semantic=parent->second;
    }
    return false;
}
bool same_reference_lineage(const Face& first,const Face& second) {
    return same_or_descendant_reference(first,second)||
        same_or_descendant_reference(second,first);
}
std::optional<Face> edge_face_fragment(const kernel::ViewerMesh& mesh,
        const Face& identity,const kernel::ViewerEdge& edge) {
    const auto support_error=[&](const Face& candidate) {
        if(!candidate.surface||edge.points.empty())
            return std::numeric_limits<double>::infinity();
        const auto& surface=*candidate.surface;
        double result{};
        for(const auto point:edge.points) {
            double error=INFINITY;
            if(surface.kind==kernel::SurfaceGeometry::Kind::Plane) {
                error=std::abs(dot(sub(point,surface.origin),
                    unit(surface.axis)));
            } else if(surface.kind==
                    kernel::SurfaceGeometry::Kind::Cylinder) {
                const auto axis=unit(surface.axis);
                const auto delta=sub(point,surface.origin);
                const auto radial=sub(delta,mul(axis,dot(delta,axis)));
                error=std::abs(std::sqrt(dot(radial,radial))-
                    surface.radius);
            }
            result=std::max(result,error);
        }
        return result;
    };
    const Face* best=nullptr;
    const Face* canonical=nullptr;
    const Face* unsupported=nullptr;
    double best_error=INFINITY;
    const auto consider_candidate=[&](const Face& candidate) {
        if(candidate!=identity)return;
        if(!canonical||(identity.surface&&candidate.surface&&
                candidate.surface->kind==identity.surface->kind&&
                (!canonical->surface||canonical->surface->kind!=
                    identity.surface->kind)))
            canonical=&candidate;
        if(candidate.surface&&candidate.surface->kind!=
                    kernel::SurfaceGeometry::Kind::Plane&&
                candidate.surface->kind!=
                    kernel::SurfaceGeometry::Kind::Cylinder)
            unsupported=&candidate;
        const double error=support_error(candidate);
        if(error<best_error) {
            best=&candidate;
            best_error=error;
        }
    };
    for(const auto& candidate:
            mesh.original_references.triangle_references)
        consider_candidate(candidate);
    // A calculated history prefix can deliberately carry no separate
    // original-reference packet. Its current triangle references still own
    // the same persisted face identity and exact analytic support, so they are
    // the authoritative fallback for reconstructing the bounded fragment.
    if(!best)for(const auto& candidate:mesh.triangle_references)
        consider_candidate(candidate);
    if(identity.surface&&canonical&&canonical->surface&&
            identity.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
            canonical->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
            std::abs(identity.surface->radius-canonical->surface->radius)>
                1e-6)
        throw std::invalid_argument(
            "Sheet conversion cannot reproduce this surface transition.");
    if(!best||best_error>1e-4) {
        struct Tangency {
            const kernel::ViewerEdge* edge;
            Face plane;
        };
        std::vector<Tangency> tangencies;
        for(const auto& candidate_edge:mesh.edges) {
            if(!straight(candidate_edge)||
                    candidate_edge.edge_treatment_side_references.size()!=2)
                continue;
            const auto& sides=candidate_edge.edge_treatment_side_references;
            const Face* other=nullptr;
            if(sides[0]==identity)other=&sides[1];
            else if(sides[1]==identity)other=&sides[0];
            if(!other)continue;
            std::optional<Face> plane;
            if(other->surface&&other->surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane)
                plane=*other;
            else {
                double plane_error=INFINITY;
                for(const auto& candidate:
                        mesh.original_references.triangle_references) {
                    if(candidate!=*other||!candidate.surface||
                            candidate.surface->kind!=
                                kernel::SurfaceGeometry::Kind::Plane)
                        continue;
                    double error{};
                    const auto axis=unit(candidate.surface->axis);
                    for(const auto point:candidate_edge.points)
                        error=std::max(error,std::abs(dot(
                            sub(point,candidate.surface->origin),axis)));
                    if(error<plane_error) {
                        plane=candidate;
                        plane_error=error;
                    }
                }
                if(plane_error>1e-4)plane.reset();
            }
            if(plane)tangencies.push_back({&candidate_edge,*plane});
        }
        std::optional<Face> inferred;
        double inferred_error=INFINITY;
        for(std::size_t first=0;first<tangencies.size();++first)
            for(std::size_t second=first+1;second<tangencies.size();++second) {
                const auto& a=tangencies[first];
                const auto& b=tangencies[second];
                const auto a_delta=sub(a.edge->points.back(),
                    a.edge->points.front());
                const auto b_delta=sub(b.edge->points.back(),
                    b.edge->points.front());
                const auto a_axis=unit(a_delta);
                const auto b_axis=unit(b_delta);
                if(std::abs(dot(a_axis,b_axis))<1-1e-8)continue;
                const auto a_point=mul(add(a.edge->points.front(),
                    a.edge->points.back()),.5);
                const auto b_point=mul(add(b.edge->points.front(),
                    b.edge->points.back()),.5);
                const auto a_normal=normal(a.plane,a_point);
                const auto b_normal=normal(b.plane,b_point);
                const auto basis=sub(a_normal,b_normal);
                const double denominator=dot(basis,basis);
                if(denominator<=1e-10)continue;
                const auto separation=sub(a_point,b_point);
                const double radius=dot(separation,basis)/denominator;
                if(radius<=1e-7)continue;
                const auto residual=sub(separation,mul(basis,radius));
                if(std::sqrt(dot(residual,residual))>1e-4)continue;
                const auto a_center=sub(a_point,mul(a_normal,radius));
                const auto b_center=sub(b_point,mul(b_normal,radius));
                if(std::sqrt(dot(sub(a_center,b_center),
                        sub(a_center,b_center)))>1e-4)continue;
                Face candidate=identity;
                kernel::SurfaceGeometry surface;
                surface.kind=kernel::SurfaceGeometry::Kind::Cylinder;
                surface.origin=mul(add(a_center,b_center),.5);
                surface.axis=a_axis;
                surface.radial=unit(sub(a_point,surface.origin));
                surface.radius=radius;
                surface.axial_min=std::min(
                    dot(sub(a.edge->points.front(),surface.origin),a_axis),
                    dot(sub(a.edge->points.back(),surface.origin),a_axis));
                surface.axial_max=std::max(
                    dot(sub(a.edge->points.front(),surface.origin),a_axis),
                    dot(sub(a.edge->points.back(),surface.origin),a_axis));
                candidate.surface=
                    std::make_shared<const kernel::SurfaceGeometry>(surface);
                const double error=support_error(candidate);
                if(error<inferred_error) {
                    inferred=std::move(candidate);
                    inferred_error=error;
                }
            }
        if(inferred&&inferred_error<=1e-4) {
            if(canonical&&canonical->surface&&
                    canonical->surface->kind==
                        kernel::SurfaceGeometry::Kind::Cylinder&&
                    std::abs(canonical->surface->radius-
                        inferred->surface->radius)>1e-6)
                throw std::invalid_argument(
                    "Sheet conversion cannot reproduce this surface transition.");
            return *inferred;
        }
    }
    if((!best||best_error>1e-4)&&identity.surface)
        return canonical?std::optional<Face>{*canonical}:
            std::optional<Face>{identity};
    if(!best||best_error>1e-4)
        return unsupported?std::optional<Face>{*unsupported}:std::nullopt;
    return *best;
}
double planar_pair_distance(const std::map<std::string,Face>& available,const Face& seed) {
    if(!seed.surface||seed.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)return 0;
    const auto n=normal(seed,seed.surface->origin);double nearest=INFINITY;
    for(const auto& [id,face]:available)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&face!=seed) {
        if(dot(n,normal(face,face.surface->origin))>-.999999)continue;
        if(face.measured_area&&seed.measured_area&&std::abs(*face.measured_area-*seed.measured_area)>.01*(*seed.measured_area))continue;
        const double distance=-dot(sub(face.surface->origin,seed.surface->origin),n);
        if(distance>.001)nearest=std::min(nearest,distance);
    }
    return std::isfinite(nearest)?nearest:0;
}
struct Node {
    Face face;
    std::optional<std::size_t> parent;
    kernel::ViewerEdge entry;
};
std::optional<V> sampled_normal(const kernel::ViewerMesh& mesh,const Face& face,V at) {
    double best=INFINITY;std::optional<V> result;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
        if(key(mesh.triangle_references[i])!=key(face)||3*i+2>=mesh.triangles.size())continue;
        const auto a=mesh.vertices.at(mesh.triangles[3*i]),b=mesh.vertices.at(mesh.triangles[3*i+1]),c=mesh.vertices.at(mesh.triangles[3*i+2]);
        const auto center=mul(add(add(a,b),c),1./3),delta=sub(center,at);
        const double distance=dot(delta,delta);const auto n=cross(sub(b,a),sub(c,a));
        if(distance<best&&dot(n,n)>1e-18){best=distance;result=unit(n);}
    }
    return result;
}
bool has_sheet_bend_exit(const kernel::ViewerMesh& mesh,
        const Face& incoming,const Face& cylinder) {
    for(const auto& edge:mesh.edges) {
        if(edge.parameter_seam||edge.points.size()<2||edge.edge_treatment_side_references.size()!=2)continue;
        const auto& sides=edge.edge_treatment_side_references;
        const auto first=edge_face_fragment(mesh,sides[0],edge);
        const auto second=edge_face_fragment(mesh,sides[1],edge);
        if(!first||!second)continue;
        const Face* next=same_fragment(*first,cylinder)?&*second:
            same_fragment(*second,cylinder)?&*first:nullptr;
        if(!next||same_fragment(*next,incoming))continue;
        if(!next->surface||next->surface->kind!=kernel::SurfaceGeometry::Kind::Plane||!straight(edge))continue;
        const auto at=edge.points[edge.points.size()/2];
        if(dot(normal(cylinder,at),normal(*next,at))<1-1e-6)continue;
        return true;
    }
    return false;
}
std::vector<Node> skin(const kernel::ViewerMesh& mesh,const Face& first) {
    std::vector<Node> result{{first,std::nullopt,{}}};
    std::vector<Face> visited{first};
    for(std::size_t i=0;i<result.size();++i) {
        const auto current=result[i].face;
        for(const auto& edge:mesh.edges) {
            if(edge.parameter_seam||edge.points.size()<2||edge.edge_treatment_side_references.size()!=2)continue;
            const auto& sides=edge.edge_treatment_side_references;
            const auto first=edge_face_fragment(mesh,sides[0],edge);
            const auto second=edge_face_fragment(mesh,sides[1],edge);
            if(!first||!second)continue;
            const Face* next=same_fragment(*first,current)?&*second:
                same_fragment(*second,current)?&*first:nullptr;
            if(!next)continue;
            if(std::ranges::any_of(visited,[&](const auto& candidate) {
                    return same_fragment(candidate,*next);
                }))continue;
            const auto at=edge.points[edge.points.size()/2];
            if(!next->surface||
                    (next->surface->kind!=
                        kernel::SurfaceGeometry::Kind::Plane&&
                     next->surface->kind!=
                        kernel::SurfaceGeometry::Kind::Cylinder)) {
                const auto n=sampled_normal(mesh,*next,at);
                // Unknown surfaces have only the calculated mesh normal here.
                // Allow its display tessellation deviation, not a planar join.
                if(n&&std::abs(dot(normal(current,at),*n))>.95)
                    throw std::invalid_argument("Sheet conversion cannot reproduce this surface transition.");
                continue;
            }
            if(dot(normal(current,at),normal(*next,at))<1-1e-6)continue;
            if(!straight(edge))throw std::invalid_argument("Sheet conversion cannot reproduce this surface transition.");
            if(current.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                    next->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                    !has_sheet_bend_exit(mesh,current,*next))continue;
            if(current.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                    next->surface->kind==kernel::SurfaceGeometry::Kind::Plane) {
                if(!result[i].parent||
                        !has_sheet_bend_exit(
                            mesh,result[*result[i].parent].face,current))
                    continue;
            }
            visited.push_back(*next);result.push_back({*next,i,edge});
        }
    }
    return result;
}
std::size_t append_imported_compound_bends(const kernel::ViewerMesh& mesh,
        std::vector<Node>& nodes,double thickness) {
    if(!std::isfinite(thickness)||thickness<=0)return 0;
    const auto same_identity=[](const Face& first,const Face& second) {
        return first.owner_id==second.owner_id&&
            first.semantic_key==second.semantic_key&&
            first.instance_path==second.instance_path;
    };
    const auto edge_length=[](const kernel::ViewerEdge& edge) {
        if(edge.points.size()<2)return 0.;
        const auto delta=sub(edge.points.back(),edge.points.front());
        return std::sqrt(dot(delta,delta));
    };
    const auto cylinder_error=[](const Face& face,
            const kernel::ViewerEdge& edge) {
        if(!face.surface||face.surface->kind!=
                    kernel::SurfaceGeometry::Kind::Cylinder||
                edge.points.empty())
            return std::numeric_limits<double>::infinity();
        const auto& surface=*face.surface;
        const auto axis=unit(surface.axis);
        double result{};
        for(const auto point:edge.points) {
            const auto delta=sub(point,surface.origin);
            const auto radial=sub(delta,mul(axis,dot(delta,axis)));
            result=std::max(result,std::abs(
                std::sqrt(dot(radial,radial))-surface.radius));
        }
        return result;
    };
    const auto coaxial=[](const Face& first,const Face& second) {
        const auto& a=*first.surface;
        const auto& b=*second.surface;
        const auto axis=unit(a.axis);
        return std::abs(std::abs(dot(axis,unit(b.axis)))-1.)<=1e-8&&
            std::sqrt(dot(cross(sub(a.origin,b.origin),axis),
                cross(sub(a.origin,b.origin),axis)))<=1e-5;
    };
    const auto original_node_count=nodes.size();
    std::set<std::string> used_transitions;
    std::size_t appended{};
    for(std::size_t plane_index=0;plane_index<original_node_count;
            ++plane_index) {
        const auto& source_node=nodes[plane_index];
        if(!source_node.face.surface||source_node.face.surface->kind!=
                kernel::SurfaceGeometry::Kind::Plane)
            continue;
        bool appended_for_plane{};
        for(const auto& entry:mesh.edges) {
            if(!straight(entry)||entry.edge_treatment_side_references.size()!=2)
                continue;
            const Face* transition_identity=nullptr;
            bool owns_entry{};
            for(const auto& identity:entry.edge_treatment_side_references) {
                if(same_identity(identity,source_node.face))owns_entry=true;
                else transition_identity=&identity;
            }
            if(!owns_entry||!transition_identity)continue;
            const double source_length=edge_length(entry);
            if(source_length<.001)continue;
            std::vector<Face> inner_candidates;
            for(const auto& candidate:
                    mesh.original_references.triangle_references)
                if(same_identity(candidate,*transition_identity)&&
                        candidate.surface&&candidate.surface->kind==
                            kernel::SurfaceGeometry::Kind::Cylinder)
                    inner_candidates.push_back(candidate);
            for(const auto& inner:inner_candidates) {
                if(cylinder_error(inner,entry)>join_tolerance||
                        dot(normal(inner,entry.points.front()),
                            normal(source_node.face,
                                entry.points.front()))<1-1e-6||
                        used_transitions.contains(key(inner))||
                        std::ranges::any_of(nodes,[&](const auto& node) {
                            return same_identity(node.face,inner);
                        }))
                    continue;
                bool has_thickness_pair{};
                for(const auto& candidate:
                        mesh.original_references.triangle_references) {
                    if(!candidate.surface||candidate.surface->kind!=
                            kernel::SurfaceGeometry::Kind::Cylinder||
                            std::abs(candidate.surface->radius-
                                inner.surface->radius-thickness)>1e-5||
                            !coaxial(inner,candidate))
                        continue;
                    has_thickness_pair=true;
                    break;
                }
                if(!has_thickness_pair)continue;
                const kernel::ViewerEdge* exit_edge=nullptr;
                std::optional<Face> target_plane;
                double exit_error=INFINITY;
                for(const auto& candidate_edge:mesh.edges) {
                    if(!straight(candidate_edge)||
                            candidate_edge.edge_treatment_side_references.
                                size()!=2||
                            std::abs(edge_length(candidate_edge)-source_length)>
                                join_tolerance)
                        continue;
                    bool owns_transition{};
                    const Face* other_identity=nullptr;
                    for(const auto& identity:
                            candidate_edge.edge_treatment_side_references) {
                        if(same_identity(identity,inner))
                            owns_transition=true;
                        else other_identity=&identity;
                    }
                    if(!owns_transition||!other_identity)continue;
                    const auto other=edge_face_fragment(
                        mesh,*other_identity,candidate_edge);
                    if(!other||!other->surface||other->surface->kind!=
                                kernel::SurfaceGeometry::Kind::Plane||
                            std::abs(dot(normal(source_node.face,
                                    entry.points.front()),
                                normal(*other,
                                    candidate_edge.points.front())))>
                                1-1e-6||
                            dot(normal(inner,
                                    candidate_edge.points.front()),
                                normal(*other,
                                    candidate_edge.points.front()))<1-1e-6)
                        continue;
                    const double error=cylinder_error(inner,candidate_edge);
                    if(error<exit_error) {
                        exit_error=error;
                        exit_edge=&candidate_edge;
                        target_plane=*other;
                    }
                }
                if(!exit_edge||!target_plane||exit_error>1e-4)continue;
                used_transitions.insert(key(inner));
                const auto bend_index=nodes.size();
                nodes.push_back({inner,plane_index,entry});
                nodes.push_back({*target_plane,bend_index,*exit_edge});
                ++appended;
                appended_for_plane=true;
                break;
            }
            if(appended_for_plane)break;
        }
    }
    return appended;
}
const kernel::ViewerEdge* bend_exit_edge(const std::vector<Node>& nodes,
        std::size_t bend) {
    for(const auto& candidate:nodes)
        if(candidate.parent&&*candidate.parent==bend&&candidate.face.surface&&
                candidate.face.surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane)
            return &candidate.entry;
    return nullptr;
}

kernel::FaceReference persistent_face_reference(const Face& face) {
    return {face.owner_id,face.semantic_key,face.instance_path};
}

kernel::EdgeReference persistent_edge_reference(
        const kernel::EdgeReference& edge) {
    return {edge.owner_id,edge.semantic_key,edge.instance_path};
}

document::SheetFromBodySurfaceSignature surface_signature(
        const Face& face) {
    if(!face.surface)throw std::invalid_argument(
        "Sheet conversion source face has no analytic support.");
    return {face.surface->kind,face.surface->origin,face.surface->axis,
        face.surface->radial,face.surface->radius,face.surface->axial_min,
        face.surface->axial_max,face.surface->reversed};
}

std::string source_region_id(const Face& face,std::string_view suffix) {
    const auto field=[](std::string_view value) {
        return std::to_string(value.size())+":"+std::string(value);
    };
    return "sheet-source-region:"+field(face.owner_id)+field(
        face.semantic_key)+field(face.instance_path)+field(suffix);
}

bool same_edge_geometry(const kernel::ViewerEdge& first,
        const kernel::ViewerEdge& second) {
    if(first.points.size()<2||second.points.size()<2)return false;
    const auto distance=[](V a,V b) {
        const auto delta=sub(a,b);return std::sqrt(dot(delta,delta));
    };
    return std::min(
        std::max(distance(first.points.front(),second.points.front()),
            distance(first.points.back(),second.points.back())),
        std::max(distance(first.points.front(),second.points.back()),
            distance(first.points.back(),second.points.front())))<=1e-5;
}

document::SheetFromBodyChildBinding source_region_binding(
        const kernel::ViewerMesh& mesh,const std::string& feature_id,
        const Face& primary,double thickness,bool root,
        std::string_view suffix,
        const std::vector<std::pair<const kernel::ViewerEdge*,
            document::SheetFromBodyEdgeRole>>& marked_edges={}) {
    using FaceRole=document::SheetFromBodyFaceRole;
    using EdgeRole=document::SheetFromBodyEdgeRole;
    if(!primary.surface||
            (primary.surface->kind!=kernel::SurfaceGeometry::Kind::Plane&&
             primary.surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder))
        throw std::invalid_argument(
            "Sheet conversion source region is not planar or cylindrical.");
    document::SheetFromBodyChildBinding result;
    result.feature_id=feature_id;
    result.region_id=source_region_id(primary,suffix);
    result.bend=primary.surface->kind==
        kernel::SurfaceGeometry::Kind::Cylinder;
    result.root=root;
    const auto face_role=[&](const Face& face,bool opposite) {
        if(!result.bend)return opposite?FaceRole::OppositeSkin:FaceRole::Skin;
        const bool inner=face.surface->reversed;
        return inner?FaceRole::BendInner:FaceRole::BendOuter;
    };
    const auto add_face=[&](const Face& face,FaceRole role) {
        if(!face.valid()||!face.surface)return;
        const auto signature=surface_signature(face);
        const auto found=std::ranges::find_if(result.faces,
            [&](const auto& existing) {
                return existing.reference==face&&
                    existing.last_valid_surface==signature;
            });
        if(found!=result.faces.end()) {
            if(found->role==FaceRole::Adjacent&&role!=FaceRole::Adjacent)
                found->role=role;
            return;
        }
        result.faces.push_back({persistent_face_reference(face),role,
            face.surface->reversed?-1:1,signature});
    };
    add_face(primary,face_role(primary,false));

    std::vector<Face> adjacent;
    const auto add_adjacent=[&](const Face& face) {
        if(std::ranges::none_of(adjacent,[&](const auto& existing) {
                return same_fragment(existing,face);
            }))adjacent.push_back(face);
        add_face(face,FaceRole::Adjacent);
    };
    for(const auto& edge:mesh.edges) {
        bool belongs{};
        for(const auto& identity:edge.edge_treatment_side_references)
            if(const auto fragment=edge_face_fragment(mesh,identity,edge);
                    fragment&&same_fragment(*fragment,primary)) {
                belongs=true;break;
            }
        if(!belongs)continue;
        EdgeRole role=EdgeRole::Boundary;
        for(const auto& [marked,marked_role]:marked_edges)
            if(marked&&(edge.reference==marked->reference||
                    same_edge_geometry(edge,*marked))) {
                role=marked_role;break;
            }
        if(edge.reference.valid()&&edge.points.size()>=2) {
            const auto found=std::ranges::find_if(result.edges,
                [&](const auto& existing) {
                    return existing.reference==edge.reference;
                });
            const std::array endpoints{edge.points.front(),edge.points.back()};
            if(found==result.edges.end())
                result.edges.push_back({persistent_edge_reference(
                    edge.reference),role,1,endpoints});
            else if(found->role==EdgeRole::Boundary&&
                    role!=EdgeRole::Boundary)found->role=role;
        }
        for(const auto& identity:edge.edge_treatment_side_references)
            if(const auto fragment=edge_face_fragment(mesh,identity,edge);
                    fragment&&!same_fragment(*fragment,primary))
                add_adjacent(*fragment);
    }

    const auto touches_adjacent=[&](const Face& candidate) {
        return std::ranges::any_of(mesh.edges,[&](const auto& edge) {
            bool candidate_side{};
            bool adjacent_side{};
            for(const auto& identity:edge.edge_treatment_side_references)
                if(const auto fragment=edge_face_fragment(mesh,identity,edge)) {
                    candidate_side|=same_fragment(*fragment,candidate);
                    adjacent_side|=std::ranges::any_of(adjacent,
                        [&](const auto& face) {
                            return same_fragment(*fragment,face);
                        });
                }
            return candidate_side&&adjacent_side;
        });
    };
    const auto paired=[&](const Face& candidate) {
        if(!candidate.surface||same_fragment(candidate,primary)||
                candidate.surface->kind!=primary.surface->kind)
            return false;
        const auto& source=*primary.surface;
        const auto& other=*candidate.surface;
        if(source.kind==kernel::SurfaceGeometry::Kind::Plane) {
            const auto axis=unit(source.axis);
            if(std::abs(std::abs(dot(axis,unit(other.axis)))-1.)>1e-8)
                return false;
            const double separation=std::abs(dot(
                sub(other.origin,source.origin),axis));
            return std::abs(separation-thickness)<=1e-5;
        }
        const auto axis=unit(source.axis);
        if(std::abs(std::abs(dot(axis,unit(other.axis)))-1.)>1e-8||
                std::sqrt(dot(cross(sub(source.origin,other.origin),axis),
                    cross(sub(source.origin,other.origin),axis)))>1e-5||
                std::abs(std::abs(source.radius-other.radius)-thickness)>
                    1e-5)
            return false;
        const double orientation=dot(axis,unit(other.axis))<0?-1:1;
        const double offset=dot(sub(other.origin,source.origin),axis);
        const double other_first=offset+orientation*other.axial_min;
        const double other_last=offset+orientation*other.axial_max;
        const double other_min=std::min(other_first,other_last);
        const double other_max=std::max(other_first,other_last);
        return std::min(source.axial_max,other_max)+1e-5>=
                std::max(source.axial_min,other_min);
    };
    const auto collect_pair=[&](const auto& candidates) {
        for(const auto& candidate:candidates)
            if(paired(candidate)&&
                    (primary.surface->kind==
                        kernel::SurfaceGeometry::Kind::Cylinder||
                     touches_adjacent(candidate)))
                add_face(candidate,face_role(candidate,true));
    };
    collect_pair(mesh.original_references.triangle_references);
    collect_pair(mesh.triangle_references);
    if(result.bend) {
        double inner_radius=std::numeric_limits<double>::infinity();
        double outer_radius=-std::numeric_limits<double>::infinity();
        for(const auto& face:result.faces)
            if(face.role==FaceRole::BendInner||
                    face.role==FaceRole::BendOuter) {
                inner_radius=std::min(inner_radius,
                    face.last_valid_surface.radius);
                outer_radius=std::max(outer_radius,
                    face.last_valid_surface.radius);
            }
        if(std::isfinite(inner_radius)&&std::isfinite(outer_radius)&&
                outer_radius-inner_radius>1e-7)
            for(auto& face:result.faces)
                if(face.role==FaceRole::BendInner||
                        face.role==FaceRole::BendOuter)
                    face.role=std::abs(face.last_valid_surface.radius-
                        inner_radius)<=std::abs(
                            face.last_valid_surface.radius-outer_radius)?
                        FaceRole::BendInner:FaceRole::BendOuter;
    }
    return result;
}

void append_source_binding(document::PartDocument& part,
        const std::string& conversion_id,
        document::SheetFromBodyChildBinding binding) {
    auto* conversion=part.find_container(conversion_id);
    if(!conversion||conversion->feature_kind!=
            document::FeatureKind::SheetFromBody)
        throw std::invalid_argument(
            "Sheet conversion lost its associative history boundary.");
    auto& bindings=conversion->sheet_from_body.child_bindings;
    if(std::ranges::any_of(bindings,[&](const auto& existing) {
            return existing.feature_id==binding.feature_id||
                existing.region_id==binding.region_id;
        }))throw std::invalid_argument(
            "Sheet conversion produced a duplicate source region.");
    bindings.push_back(std::move(binding));
}

struct ContinuationBend {
    // The source Flat owns the Bend attachment. The target Flat is split into
    // one region per incoming branch; edges use the same source/target order.
    std::array<std::size_t,2> planes;
    std::array<kernel::ViewerEdge,2> edges;
    Face transition;
    kernel::SurfaceGeometry surface;
};
std::vector<ContinuationBend> infer_corner_continuations(
        const kernel::ViewerMesh& mesh,const std::vector<Node>& nodes) {
    struct Boundary {
        std::size_t plane;
        kernel::ViewerEdge edge;
        Face transition;
    };
    const auto same_edge=[](const kernel::ViewerEdge& first,
            const kernel::ViewerEdge& second) {
        if(first.points.empty()||second.points.empty())return false;
        const auto distance=[](V a,V b) {
            const auto delta=sub(a,b);
            return std::sqrt(dot(delta,delta));
        };
        return std::min(
            std::max(distance(first.points.front(),second.points.front()),
                distance(first.points.back(),second.points.back())),
            std::max(distance(first.points.front(),second.points.back()),
                distance(first.points.back(),second.points.front())))<=1e-5;
    };
    std::vector<Boundary> boundaries;
    for(std::size_t index=0;index<nodes.size();++index) {
        const auto& node=nodes[index];
        if(!node.face.surface||node.face.surface->kind!=
                kernel::SurfaceGeometry::Kind::Plane)continue;
        for(const auto& edge:mesh.edges) {
            if(edge.parameter_seam||!straight(edge)||
                    edge.edge_treatment_side_references.size()!=2)continue;
            bool belongs{};
            for(const auto& identity:edge.edge_treatment_side_references) {
                const auto fragment=edge_face_fragment(mesh,identity,edge);
                if(fragment&&same_fragment(*fragment,node.face)) {
                    belongs=true;
                    break;
                }
            }
            if(!belongs)continue;
            for(const auto& identity:edge.edge_treatment_side_references) {
                const auto fragment=edge_face_fragment(mesh,identity,edge);
                if(fragment&&same_fragment(*fragment,node.face))continue;
                // A neighbour already present in the ordinary skin walk is a
                // real analytic transition only on the exact entry/exit edge
                // used by that walk. A fillet identity may also own a corner
                // patch after a later crossing fillet; its other boundary
                // edges mark the omitted continuation between found branches.
                bool ordinary_connection{};
                for(std::size_t candidate_index=1;
                        candidate_index<nodes.size();++candidate_index) {
                    const auto& candidate=nodes[candidate_index];
                    if(!same_edge(edge,candidate.entry))continue;
                    ordinary_connection=!candidate.face.surface||
                        candidate.face.surface->kind!=
                            kernel::SurfaceGeometry::Kind::Cylinder||
                        bend_exit_edge(nodes,candidate_index);
                    break;
                }
                if(fragment&&ordinary_connection)continue;
                boundaries.push_back({index,edge,identity});
            }
        }
    }
    const auto distance=[](V first,V second) {
        const auto delta=sub(first,second);
        return std::sqrt(dot(delta,delta));
    };
    std::vector<ContinuationBend> result;
    for(std::size_t first_index=0;first_index<boundaries.size();++first_index)
        for(std::size_t second_index=first_index+1;
                second_index<boundaries.size();++second_index) {
            const auto& first=boundaries[first_index];
            const auto& second=boundaries[second_index];
            if(first.plane==second.plane||
                    key(first.transition)!=key(second.transition))continue;
            const auto& first_node=nodes[first.plane];
            const auto& second_node=nodes[second.plane];
            if(!first_node.parent||!second_node.parent||
                    *first_node.parent==*second_node.parent)continue;
            const auto& first_parent=nodes[*first_node.parent];
            const auto& second_parent=nodes[*second_node.parent];
            if(!first_parent.face.surface||!second_parent.face.surface||
                    first_parent.face.surface->kind!=
                        kernel::SurfaceGeometry::Kind::Cylinder||
                    second_parent.face.surface->kind!=
                        kernel::SurfaceGeometry::Kind::Cylinder||
                    !first_parent.parent||!second_parent.parent||
                    *first_parent.parent!=*second_parent.parent)continue;
            const auto first_delta=sub(first.edge.points.back(),
                first.edge.points.front());
            const auto second_delta=sub(second.edge.points.back(),
                second.edge.points.front());
            const double first_length=std::sqrt(dot(first_delta,first_delta));
            const double second_length=std::sqrt(dot(second_delta,second_delta));
            if(first_length<.001||
                    std::abs(first_length-second_length)>1e-5)continue;
            const auto first_direction=mul(first_delta,1/first_length);
            const auto second_direction=mul(second_delta,1/second_length);
            if(std::abs(dot(first_direction,second_direction))<1-1e-8)
                continue;
            double radius=INFINITY;
            for(const auto& candidate:
                    mesh.original_references.triangle_references)
                if(candidate==first.transition&&candidate.surface&&
                        candidate.surface->kind==
                            kernel::SurfaceGeometry::Kind::Cylinder)
                    radius=std::min(radius,candidate.surface->radius);
            if(!std::isfinite(radius)||radius<=1e-7)continue;
            const bool reversed=
                distance(first.edge.points.front(),second.edge.points.back())+
                    distance(first.edge.points.back(),second.edge.points.front())<
                distance(first.edge.points.front(),second.edge.points.front())+
                    distance(first.edge.points.back(),second.edge.points.back());
            const std::array first_points{first.edge.points.front(),
                first.edge.points.back()};
            const std::array second_points{
                reversed?second.edge.points.back():second.edge.points.front(),
                reversed?second.edge.points.front():second.edge.points.back()};
            const auto first_normal=normal(first_node.face,first_points[0]);
            const auto second_normal=normal(second_node.face,second_points[0]);
            std::optional<V> axis_origin;
            for(const double first_sign:{-1.,1.})
                for(const double second_sign:{-1.,1.}) {
                    const auto first_center=add(first_points[0],
                        mul(first_normal,-first_sign*radius));
                    const auto first_last_center=add(first_points[1],
                        mul(first_normal,-first_sign*radius));
                    const auto second_center=add(second_points[0],
                        mul(second_normal,-second_sign*radius));
                    const auto second_last_center=add(second_points[1],
                        mul(second_normal,-second_sign*radius));
                    if(distance(first_center,second_center)<=1e-5&&
                            distance(first_last_center,second_last_center)<=1e-5)
                        axis_origin=first_center;
                }
            if(!axis_origin)continue;
            kernel::SurfaceGeometry surface;
            surface.kind=kernel::SurfaceGeometry::Kind::Cylinder;
            surface.origin=*axis_origin;
            surface.axis=first_direction;
            surface.radial=unit(sub(first_points[0],*axis_origin));
            surface.radius=radius;
            surface.axial_min=0;
            surface.axial_max=first_length;
            auto second_edge=second.edge;
            if(reversed)std::reverse(second_edge.points.begin(),
                second_edge.points.end());
            result.push_back({{first.plane,second.plane},
                {first.edge,std::move(second_edge)},first.transition,surface});
        }
    std::map<std::size_t,std::size_t> degree;
    for(const auto& continuation:result)
        for(const auto plane:continuation.planes)++degree[plane];
    for(auto& continuation:result) {
        const auto first=continuation.planes[0];
        const auto second=continuation.planes[1];
        // A face shared by several omitted-corner continuations is the common
        // branch Flat and remains whole. Each less-connected end face is the
        // continuation target and is partitioned at the angular bisector.
        // A single-corner tie has two equivalent trees; keep the later skin
        // node as the target.
        if(degree[first]<degree[second]||
                (degree[first]==degree[second]&&first>second)) {
            std::swap(continuation.planes[0],continuation.planes[1]);
            std::swap(continuation.edges[0],continuation.edges[1]);
        }
    }
    return result;
}
void numeric_frame(document::HistoryContainer& feature,sketcher::Sketch& sketch,V origin,V x,V y,V z) {
    const double ry=std::asin(std::clamp(-x.z,-1.,1.));
    const double rx=std::abs(std::cos(ry))>1e-9?std::atan2(y.z,z.z):0;
    const double rz=std::abs(std::cos(ry))>1e-9?std::atan2(x.y,x.x):std::atan2(-y.x,y.y);
    auto& p=feature.placement;p.x=origin.x;p.y=origin.y;p.z=origin.z;
    p.absolute_rotation_x=p.rotation_x=rx*180/std::numbers::pi;
    p.absolute_rotation_y=p.rotation_y=ry*180/std::numbers::pi;
    p.absolute_rotation_z=p.rotation_z=rz*180/std::numbers::pi;
    sketch.plane=sketcher::SketchPlane::XY;sketch.plane_auto=false;
    sketch.resolved_origin=origin;sketch.resolved_x_axis=x;sketch.resolved_y_axis=y;sketch.resolved_normal=z;
}
struct RootSheetFrame {V origin,x,y,normal;};
RootSheetFrame root_sheet_frame(const document::PartDocument& source,
        const sketcher::Sketch& local_context,const kernel::ViewerMesh& mesh,const Face& face) {
    const auto n=unit(normal(face,face.surface->origin));
    auto fallback_x=unit(face.surface->radial);
    // The Part Origin is document geometry. Convert it through the same public
    // Body-local path as the selected source packet; no placement references or
    // general placement equations are changed by this conversion-only seed.
    auto origin_geometry=source.sketch_reference_geometry_for(
        local_context,source.origin_viewer_mesh().original_references);
    if(origin_geometry.points.empty()||origin_geometry.axes.size()<3)
        return {face.surface->origin,fallback_x,cross(n,fallback_x),n};
    const auto part_origin=origin_geometry.points.front().position;
    std::array<V,3> part_axes;
    for(std::size_t i=0;i<part_axes.size();++i)part_axes[i]=unit(origin_geometry.axes[i].direction);
    const auto inside=[&](V p) {
        for(std::size_t triangle=0;triangle<mesh.triangle_references.size();++triangle) {
            if(key(mesh.triangle_references[triangle])!=key(face)||3*triangle+2>=mesh.triangles.size())continue;
            const auto a=mesh.vertices.at(mesh.triangles[3*triangle]);
            const auto b=mesh.vertices.at(mesh.triangles[3*triangle+1]);
            const auto c=mesh.vertices.at(mesh.triangles[3*triangle+2]);
            const auto v0=sub(b,a),v1=sub(c,a),v2=sub(p,a);
            const double d00=dot(v0,v0),d01=dot(v0,v1),d11=dot(v1,v1);
            const double d20=dot(v2,v0),d21=dot(v2,v1),denominator=d00*d11-d01*d01;
            if(std::abs(denominator)<=1e-20)continue;
            const double v=(d11*d20-d01*d21)/denominator;
            const double w=(d00*d21-d01*d20)/denominator;
            const double u=1-v-w;
            if(u>=-1e-8&&v>=-1e-8&&w>=-1e-8&&
               std::abs(dot(sub(p,a),n))<=1e-6)return true;
        }
        return false;
    };
    const auto plane_count=[&](V p) {
        unsigned count{};
        for(const auto axis:part_axes)if(std::abs(dot(sub(p,part_origin),axis))<=1e-6)++count;
        return count;
    };
    struct Candidate {V point;unsigned planes{};double distance{};};
    std::vector<Candidate> candidates;
    const auto offer=[&](V p) {
        if(!inside(p))return;
        const auto delta=sub(p,part_origin);
        candidates.push_back({p,plane_count(p),dot(delta,delta)});
    };
    // Orthogonal projection is the least-surprising fallback whenever the
    // selected bounded face contains it.
    offer(add(part_origin,mul(n,dot(sub(face.surface->origin,part_origin),n))));
    // Intersections with a Part axis lie in two Origin planes at once.
    for(const auto axis:part_axes) {
        const double denominator=dot(n,axis);
        if(std::abs(denominator)>1e-10)
            offer(add(part_origin,mul(axis,dot(n,sub(face.surface->origin,part_origin))/denominator)));
    }
    // Otherwise use an actual bounded intersection with one Origin plane.
    // Triangle edges preserve holes and concave outlines without an untrimmed
    // supporting-plane shortcut.
    for(const auto plane_normal:part_axes)
        for(std::size_t triangle=0;triangle<mesh.triangle_references.size();++triangle) {
            if(key(mesh.triangle_references[triangle])!=key(face)||3*triangle+2>=mesh.triangles.size())continue;
            const std::array vertices{mesh.vertices.at(mesh.triangles[3*triangle]),
                mesh.vertices.at(mesh.triangles[3*triangle+1]),mesh.vertices.at(mesh.triangles[3*triangle+2])};
            for(std::size_t edge=0;edge<3;++edge) {
                const auto a=vertices[edge],b=vertices[(edge+1)%3];
                const double da=dot(sub(a,part_origin),plane_normal);
                const double db=dot(sub(b,part_origin),plane_normal);
                if(std::abs(da)<=1e-10)offer(a);
                if((da<0&&db>0)||(da>0&&db<0))offer(add(a,mul(sub(b,a),da/(da-db))));
            }
        }
    V chosen_origin=face.surface->origin;
    if(!candidates.empty()) {
        const auto chosen=std::ranges::max_element(candidates,[](const auto& a,const auto& b) {
            if(a.planes!=b.planes)return a.planes<b.planes;
            return a.distance>b.distance;
        });
        chosen_origin=chosen->point;
    }
    // Keep the selected face normal/material side.  Within that plane prefer
    // a projected Part axis closest to the source face's established radial
    // direction, so XY/XZ/YZ faces receive their natural local axes without
    // an arbitrary 90-degree turn.
    double best_alignment=-1;
    for(const auto axis:part_axes) {
        const auto projected=sub(axis,mul(n,dot(axis,n)));
        const double length=std::sqrt(dot(projected,projected));
        if(length<=1e-10)continue;
        auto direction=mul(projected,1/length);
        double alignment=dot(direction,fallback_x);
        if(alignment<0){direction=mul(direction,-1);alignment=-alignment;}
        if(alignment>best_alignment){best_alignment=alignment;fallback_x=direction;}
    }
    return {chosen_origin,fallback_x,cross(n,fallback_x),n};
}
void outline(sketcher::Sketch& sketch,const kernel::ViewerMesh& mesh,const Face& face) {
    struct BoundaryEdge {
        const kernel::ViewerEdge* edge;
        std::vector<Face> sides;
    };
    std::vector<BoundaryEdge> boundary;
    std::vector<V> face_vertices;
    for(std::size_t triangle=0;triangle<mesh.triangle_references.size();
            ++triangle) {
        if(!same_fragment(mesh.triangle_references[triangle],face)||
                3*triangle+2>=mesh.triangles.size())continue;
        for(std::size_t corner=0;corner<3;++corner)
            face_vertices.push_back(mesh.vertices.at(
                mesh.triangles[3*triangle+corner]));
    }
    const auto has_face_vertex=[&](V point) {
        return std::ranges::any_of(face_vertices,[&](V candidate) {
            const auto delta=sub(candidate,point);
            return dot(delta,delta)<=1e-10;
        });
    };
    const auto duplicate=[&](const kernel::ViewerEdge& candidate) {
        const auto same_points=[&](const kernel::ViewerEdge& existing,
                bool reverse) {
            if(existing.points.size()!=candidate.points.size())return false;
            for(std::size_t i=0;i<candidate.points.size();++i) {
                const auto j=reverse?candidate.points.size()-1-i:i;
                const auto delta=sub(existing.points[i],candidate.points[j]);
                if(dot(delta,delta)>1e-10)return false;
            }
            return true;
        };
        return std::ranges::any_of(boundary,[&](const BoundaryEdge& existing) {
            return same_points(*existing.edge,false)||
                same_points(*existing.edge,true);
        });
    };
    for(const auto& edge:mesh.edges) {
        std::vector<Face> sides;
        for(const auto& identity:edge.edge_treatment_side_references)
            if(auto fragment=edge_face_fragment(mesh,identity,edge))
                sides.push_back(std::move(*fragment));
        // Identity is sufficient to establish boundary ownership. Fragment
        // recovery adds the supporting surface needed for curved borders, but
        // must not make a straight native outline disappear when a compact
        // calculated cache omits its original-reference packet.
        const bool belongs_by_identity=std::ranges::any_of(
                edge.edge_treatment_side_references,
                [&](const auto& side){return side==face;})||
            std::ranges::any_of(sides,
                [&](const auto& side){return same_fragment(side,face);});
        // An open RIP duplicates its new material boundary between the two
        // partition solids. OCCT does not publish a two-sided treatment pair
        // for that edge, but its exact endpoints remain vertices of each
        // bounded face tessellation. Recover only this otherwise unowned
        // boundary and collapse the coincident copy; ordinary referenced
        // edges continue to use their persisted face identity.
        const bool belongs_by_partition=edge.edge_treatment_side_references.empty()&&
            edge.points.size()>=2&&has_face_vertex(edge.points.front())&&
            has_face_vertex(edge.points.back());
        if(edge.parameter_seam||(!belongs_by_identity&&!belongs_by_partition)||
                duplicate(edge))continue;
        boundary.push_back({&edge,std::move(sides)});
    }
    struct CircularEdge {
        std::array<double,2> center,a,b,m;
        double radius{};
    };
    const auto circular=[&](const BoundaryEdge& boundary_edge) ->
            std::optional<CircularEdge> {
        const auto& edge=*boundary_edge.edge;
        std::optional<std::array<double,2>> circle_center;
        double radius{};
        for(const auto& side:boundary_edge.sides)
            if(!same_fragment(side,face)&&side.surface) {
                const auto& surface=*side.surface;
                if(surface.kind!=kernel::SurfaceGeometry::Kind::Cylinder||
                        std::abs(dot(surface.axis,
                            sketch.resolved_normal))<1-1e-7)
                    continue;
                circle_center=sketch.local_point(surface.origin);
                radius=surface.radius;
            }
        if(!circle_center||edge.points.size()<=2)return std::nullopt;
        const auto center=*circle_center;
        for(const auto point:edge.points) {
            const auto local=sketch.local_point(point);
            if(std::abs(std::hypot(local[0]-center[0],
                    local[1]-center[1])-radius)>1e-5)
                return std::nullopt;
        }
        const auto project=[&](std::array<double,2> point) {
            const double length=std::hypot(point[0]-center[0],
                point[1]-center[1]);
            if(length<=1e-12)return point;
            point[0]=center[0]+radius*(point[0]-center[0])/length;
            point[1]=center[1]+radius*(point[1]-center[1])/length;
            return point;
        };
        return CircularEdge{center,project(sketch.local_point(
            edge.points.front())),project(sketch.local_point(
            edge.points.back())),project(sketch.local_point(
            edge.points[edge.points.size()/2])),radius};
    };
    std::vector<bool> consumed(boundary.size());
    for(std::size_t edge_index=0;edge_index<boundary.size();++edge_index) {
        if(consumed[edge_index])continue;
        const auto& edge=*boundary[edge_index].edge;
        if(straight(edge)) {
            const auto a=sketch.local_point(edge.points.front()),b=sketch.local_point(edge.points.back());
            static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1],1e-6,false));continue;
        }
        if(const auto circle=circular(boundary[edge_index])) {
            const auto& [center,a,b,m,radius]=*circle;
            if(std::hypot(a[0]-b[0],a[1]-b[1])<1e-6) {
                static_cast<void>(sketch.add_circle(center[0],center[1],radius));
                continue;
            }
            bool merged{};
            for(std::size_t other_index=edge_index+1;
                    other_index<boundary.size();++other_index) {
                if(consumed[other_index])continue;
                const auto other=circular(boundary[other_index]);
                if(!other||std::hypot(other->center[0]-center[0],
                            other->center[1]-center[1])>1e-5||
                        std::abs(other->radius-radius)>1e-5)
                    continue;
                const bool same_ends=(std::hypot(other->a[0]-a[0],
                            other->a[1]-a[1])<=1e-5&&
                        std::hypot(other->b[0]-b[0],
                            other->b[1]-b[1])<=1e-5)||
                    (std::hypot(other->a[0]-b[0],
                            other->a[1]-b[1])<=1e-5&&
                        std::hypot(other->b[0]-a[0],
                            other->b[1]-a[1])<=1e-5);
                const auto chord_side=[&](const auto& point) {
                    return (b[0]-a[0])*(point[1]-a[1])-
                        (b[1]-a[1])*(point[0]-a[0]);
                };
                if(!same_ends||chord_side(m)*chord_side(other->m)>=-1e-10)
                    continue;
                consumed[other_index]=true;
                static_cast<void>(sketch.add_circle(center[0],center[1],radius));
                merged=true;
                break;
            }
            if(merged)continue;
            // OCCT can report a circular endpoint a few tenths of a micron
            // away from its analytic cylinder. Keep a shared native Sketch
            // point on the exact circle for non-closed curved boundaries.
            for(const auto point:{a,b}) {
                const auto existing=std::ranges::find_if(sketch.points,
                    [&](const auto& candidate) {
                        return std::hypot(candidate.x-point[0],
                            candidate.y-point[1])<=1e-5;
                    });
                if(existing!=sketch.points.end()) {
                    existing->x=point[0];existing->y=point[1];
                }
            }
            const double start=std::atan2(a[1]-center[1],a[0]-center[0]);
            const auto angle=[&](const auto& point) {
                double value=std::atan2(point[1]-center[1],
                    point[0]-center[0])-start;
                return value<0?value+2*std::numbers::pi:value;
            };
            static_cast<void>(sketch.add_arc(center[0],center[1],a[0],a[1],
                b[0],b[1],false,1e-7,angle(m)>angle(b)));
            continue;
        }
        const auto original=std::ranges::find_if(mesh.original_references.edges,[&](const auto& e){return e.reference==edge.reference;});
        const auto* exact=edge.exact_spline?&*edge.exact_spline:original!=mesh.original_references.edges.end()&&original->exact_spline?&*original->exact_spline:nullptr;
        if(exact) {
            std::vector<std::array<double,2>> poles;for(auto p:exact->poles)poles.push_back(sketch.local_point(p));
            const auto id=sketch.add_bspline(poles,exact->degree,false,false,1e-6,false);
            auto& spline=*std::ranges::find(sketch.bsplines,id,&sketcher::SketchBSpline::id);spline.knots=exact->knots;spline.weights=exact->weights;
        }else for(std::size_t i=1;i<edge.points.size();++i){const auto a=sketch.local_point(edge.points[i-1]),b=sketch.local_point(edge.points[i]);static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1],1e-6,false));}
    }
}
void split_outline(sketcher::Sketch& sketch,const kernel::ViewerMesh& mesh,
        const Face& face,const std::vector<kernel::ViewerEdge>& incoming,
        std::size_t region) {
    if(incoming.size()<2||region>=incoming.size()) {
        outline(sketch,mesh,face);
        return;
    }
    constexpr double point_tolerance=1e-6;
    std::vector<V> vertices;
    std::vector<std::array<std::size_t,2>> segments;
    const auto vertex=[&](V point) {
        const auto found=std::ranges::find_if(vertices,[&](V candidate) {
            const auto delta=sub(candidate,point);
            return dot(delta,delta)<=point_tolerance*point_tolerance;
        });
        if(found!=vertices.end())return static_cast<std::size_t>(
            found-vertices.begin());
        vertices.push_back(point);
        return vertices.size()-1;
    };
    for(const auto& edge:mesh.edges) {
        if(edge.parameter_seam||edge.points.size()<2)continue;
        bool belongs{};
        for(const auto& identity:edge.edge_treatment_side_references) {
            const auto fragment=edge_face_fragment(mesh,identity,edge);
            if(fragment&&same_fragment(*fragment,face)) {
                belongs=true;
                break;
            }
        }
        if(!belongs)continue;
        // A corner partition must retain exact editable source geometry. The
        // current rule is intentionally limited to straight planar borders;
        // do not replace a curved border by display tessellation.
        if(!straight(edge))throw std::invalid_argument(
            "Sheet conversion cannot split a curved continuation face.");
        const std::array connection{vertex(edge.points.front()),
            vertex(edge.points.back())};
        if(connection[0]==connection[1])continue;
        const bool duplicate=std::ranges::any_of(segments,
            [&](const auto& existing) {
                return existing==connection||
                    (existing[0]==connection[1]&&
                     existing[1]==connection[0]);
            });
        if(!duplicate)segments.push_back(connection);
    }
    if(vertices.size()<3||segments.size()!=vertices.size())
        throw std::invalid_argument(
            "Sheet conversion cannot order the continuation face.");
    std::vector<std::vector<std::size_t>> adjacent(vertices.size());
    for(const auto& segment:segments) {
        adjacent[segment[0]].push_back(segment[1]);
        adjacent[segment[1]].push_back(segment[0]);
    }
    if(std::ranges::any_of(adjacent,
            [](const auto& neighbours){return neighbours.size()!=2;}))
        throw std::invalid_argument(
            "Sheet conversion cannot split a non-manifold continuation face.");
    std::vector<std::size_t> order{0};
    std::size_t previous=std::numeric_limits<std::size_t>::max();
    std::size_t current=0;
    while(true) {
        const auto& neighbours=adjacent[current];
        const auto next=neighbours[0]==previous?neighbours[1]:neighbours[0];
        if(next==order.front())break;
        if(std::ranges::find(order,next)!=order.end()||
                order.size()>=vertices.size())
            throw std::invalid_argument(
                "Sheet conversion cannot order the continuation face.");
        order.push_back(next);
        previous=current;
        current=next;
    }
    if(order.size()!=vertices.size())throw std::invalid_argument(
        "Sheet conversion cannot split multiple continuation loops.");
    using P=std::array<double,2>;
    std::vector<P> polygon;
    for(const auto index:order)polygon.push_back(
        sketch.local_point(vertices[index]));
    P centroid{};
    for(const auto point:polygon) {
        centroid[0]+=point[0];
        centroid[1]+=point[1];
    }
    centroid[0]/=polygon.size();
    centroid[1]/=polygon.size();
    struct Line {P normal;double constant;};
    std::vector<Line> borders;
    for(const auto& edge:incoming) {
        if(!straight(edge))throw std::invalid_argument(
            "Sheet conversion found a curved continuation entry.");
        const auto first=sketch.local_point(edge.points.front());
        const auto last=sketch.local_point(edge.points.back());
        const P direction{last[0]-first[0],last[1]-first[1]};
        const double length=std::hypot(direction[0],direction[1]);
        if(length<=point_tolerance)throw std::invalid_argument(
            "Sheet conversion found a zero-length continuation entry.");
        Line line{{-direction[1]/length,direction[0]/length},0};
        line.constant=-line.normal[0]*first[0]-line.normal[1]*first[1];
        const auto value=[&](P point) {
            return line.normal[0]*point[0]+line.normal[1]*point[1]+
                line.constant;
        };
        if(value(centroid)<0) {
            line.normal[0]*=-1;
            line.normal[1]*=-1;
            line.constant*=-1;
        }
        // Every entry must be a supporting border of this convex source
        // region. This makes its positive value the exact in-face distance.
        if(std::ranges::any_of(polygon,[&](P point) {
                return line.normal[0]*point[0]+
                    line.normal[1]*point[1]+line.constant<-1e-5;
            }))
            throw std::invalid_argument(
                "Sheet conversion cannot bisect a concave continuation face.");
        borders.push_back(line);
    }
    const auto clip=[&](std::vector<P> input,const Line& keep,
            const Line& other) {
        std::vector<P> output;
        if(input.empty())return output;
        const auto value=[&](P point) {
            const double keep_distance=keep.normal[0]*point[0]+
                keep.normal[1]*point[1]+keep.constant;
            const double other_distance=other.normal[0]*point[0]+
                other.normal[1]*point[1]+other.constant;
            return other_distance-keep_distance;
        };
        auto previous=input.back();
        double previous_value=value(previous);
        for(const auto point:input) {
            const double point_value=value(point);
            const bool previous_inside=previous_value>=-1e-9;
            const bool point_inside=point_value>=-1e-9;
            if(previous_inside!=point_inside) {
                const double denominator=previous_value-point_value;
                if(std::abs(denominator)>1e-14) {
                    const double fraction=previous_value/denominator;
                    output.push_back({
                        previous[0]+fraction*(point[0]-previous[0]),
                        previous[1]+fraction*(point[1]-previous[1])});
                }
            }
            if(point_inside)output.push_back(point);
            previous=point;
            previous_value=point_value;
        }
        return output;
    };
    for(std::size_t other=0;other<borders.size();++other)
        if(other!=region)
            polygon=clip(std::move(polygon),borders[region],borders[other]);
    const auto same=[](P first,P second) {
        return std::hypot(first[0]-second[0],first[1]-second[1])<=1e-6;
    };
    std::vector<P> clean;
    for(const auto point:polygon)
        if(clean.empty()||!same(clean.back(),point))clean.push_back(point);
    if(clean.size()>1&&same(clean.front(),clean.back()))clean.pop_back();
    double area{};
    for(std::size_t index=0;index<clean.size();++index) {
        const auto& first=clean[index];
        const auto& second=clean[(index+1)%clean.size()];
        area+=first[0]*second[1]-first[1]*second[0];
    }
    if(clean.size()<3||std::abs(area)<=1e-8)
        throw std::invalid_argument(
            "Sheet conversion produced an empty continuation region.");
    for(std::size_t index=0;index<clean.size();++index) {
        const auto& first=clean[index];
        const auto& second=clean[(index+1)%clean.size()];
        static_cast<void>(sketch.add_segment(first[0],first[1],
            second[0],second[1],1e-6,false));
    }
}
void bind_flat_outline_to_attachment(sketcher::Sketch& sketch,
        double endpoint_tolerance,bool translate_adjacent_sides) {
    const auto endpoint=[&](const char* suffix) {
        return std::ranges::find(sketch.external_references,sketch.id+suffix,
            &sketcher::SketchExternalReference::id);
    };
    const auto first=endpoint(":flat-attachment:first");
    const auto last=endpoint(":flat-attachment:last");
    if(first==sketch.external_references.end()||last==sketch.external_references.end()||
            first->broken||last->broken||first->cached_points.size()!=1||
            last->cached_points.size()!=1)
        throw std::invalid_argument("Flat profile conflicts with its sheet endpoint references.");
    // Select one real outline segment by both endpoints together. Choosing
    // each nearest point independently can bind two different perimeter
    // edges at a corner: the Flat then follows only one endpoint of the Bend
    // clearance instead of moving its complete joining edge.
    const auto distance=[](const auto& point,const auto& target) {
        return std::hypot(point->x-target[0],point->y-target[1]);
    };
    const auto first_target=first->cached_points.front();
    const auto last_target=last->cached_points.front();
    const sketcher::SketchSegment* joining_segment=nullptr;
    bool reversed{};double best=INFINITY;
    for(const auto& segment:sketch.segments) {
        if(segment.construction)continue;
        const auto* a=sketch.find_point(segment.first_point_id);
        const auto* b=sketch.find_point(segment.second_point_id);
        if(!a||!b)continue;
        const double direct=std::max(
            distance(a,first_target),distance(b,last_target));
        const double reverse=std::max(
            distance(a,last_target),distance(b,first_target));
        const double error=std::min(direct,reverse);
        if(error<best) {
            best=error;joining_segment=&segment;reversed=reverse<direct;
        }
    }
    // The initial Bend uses the source solid's exact outside endpoints. The
    // measured inner-corner correction is applied only after every adjacent
    // Bend is available, so this first attachment must match the source edge.
    if(!joining_segment||best>endpoint_tolerance+join_tolerance)
        throw std::invalid_argument(
            "Flat profile conflicts with its sheet endpoint references.");
    const auto first_point_id=reversed
        ?joining_segment->second_point_id:joining_segment->first_point_id;
    const auto last_point_id=reversed
        ?joining_segment->first_point_id:joining_segment->second_point_id;
    // Only the shared joining edge follows the final Bend end.  The remaining
    // vertices are the source planar face's exact authored outline.  Moving
    // each complete adjacent side by a different endpoint correction shears
    // the opposite Flat edge and can make neighbouring branches overlap.
    // Two direct point references express the required geometry without any
    // persisted construction guide.
    auto* first_point=sketch.find_point(first_point_id);
    auto* last_point=sketch.find_point(last_point_id);
    if(!first_point||!last_point)
        throw std::invalid_argument(
            "Flat profile conflicts with its sheet endpoint references.");
    const auto joining_segment_id=joining_segment->id;
    const auto move_side=[&](const std::string& joining_id,
            const std::string& other_joining_id,
            const std::array<double,2>& target) {
        auto* joining=sketch.find_point(joining_id);
        if(!joining)throw std::invalid_argument(
            "Flat profile conflicts with its sheet endpoint references.");
        const std::array delta{target[0]-joining->x,target[1]-joining->y};
        if(!translate_adjacent_sides) {
            joining->x=target[0];joining->y=target[1];
            return;
        }
        std::string remote_id;
        for(const auto& segment:sketch.segments) {
            if(segment.construction||segment.id==joining_segment_id)continue;
            if(segment.first_point_id==joining_id&&
                    segment.second_point_id!=other_joining_id)
                remote_id=segment.second_point_id;
            else if(segment.second_point_id==joining_id&&
                    segment.first_point_id!=other_joining_id)
                remote_id=segment.first_point_id;
        }
        auto* remote=sketch.find_point(remote_id);
        if(remote_id.empty()||!remote)throw std::invalid_argument(
            "Flat profile conflicts with its sheet endpoint references.");
        remote->x+=delta[0];remote->y+=delta[1];
        joining->x=target[0];joining->y=target[1];
    };
    move_side(first_point_id,last_point_id,first_target);
    move_side(last_point_id,first_point_id,last_target);
    std::map<std::string,bool> retained_fixed_state;
    for(auto& point:sketch.points)
        if(point.id!=first_point_id&&point.id!=last_point_id) {
            retained_fixed_state.emplace(point.id,point.fixed);
            point.fixed=true;
        }
    const auto first_reference_id=first->id,last_reference_id=last->id;
    // Adding the first equation replaces the Sketch transactionally and
    // invalidates every vector iterator. Persist identities before either add.
    const auto bind=[&](const std::string& point,const std::string& reference) {
        if(std::ranges::none_of(sketch.constraints,[&](const auto& constraint) {
                return !constraint.suppressed&&
                    constraint.kind==sketcher::ConstraintKind::PointReference&&
                    constraint.first_point_id==point&&
                    constraint.second_point_id==reference;
            }))
            static_cast<void>(sketch.add_point_reference_constraint(point,reference));
    };
    bind(first_point_id,first_reference_id);
    bind(last_point_id,last_reference_id);
    // Validate while the unrelated outline vertices are temporary solver
    // anchors. Their authored fixed state is restored immediately afterwards;
    // this seeding aid never becomes a user-visible Fixed relation.
    const auto solved=sketch.solve();
    if(solved.status==sketcher::SolveStatus::Conflicting||
            solved.status==sketcher::SolveStatus::Invalid)
        throw std::invalid_argument("Flat profile conflicts with its sheet endpoint references.");
    for(const auto& [id,fixed]:retained_fixed_state)
        if(auto* point=sketch.find_point(id))point->fixed=fixed;
}
const kernel::ViewerEdge& bend_preview_edge(const kernel::ViewerMesh& preview,
        std::string_view role) {
    const auto edge=std::ranges::find_if(preview.edges,[&](const auto& value) {
        return value.reference.semantic_key.find(role)!=std::string::npos;
    });
    if(edge==preview.edges.end()||edge->points.empty())
        throw std::invalid_argument(
            "Sheet conversion cannot measure a Bend corner endpoint.");
    return *edge;
}
std::size_t bend_meeting_end(const document::HistoryContainer& feature,
        const sketcher::Sketch& sketch,V meeting,
        const document::SheetMetalDefaults& defaults) {
    const auto preview=document::bend_preview(feature,sketch,defaults);
    const auto& first=bend_preview_edge(preview,"outer-first");
    const auto& last=bend_preview_edge(preview,"outer-last");
    const auto distance_squared=[&](V point) {
        const auto delta=sub(point,meeting);return dot(delta,delta);
    };
    return distance_squared(last.points.front())<
        distance_squared(first.points.front())?1:0;
}
V bend_inner_end(const document::HistoryContainer& feature,
        const sketcher::Sketch& sketch,std::size_t end,
        const document::SheetMetalDefaults& defaults) {
    const auto preview=document::bend_preview(feature,sketch,defaults);
    return bend_preview_edge(preview,end?"inner-last":"inner-first").
        points.back();
}
bool apply_measured_bend_corner_corrections(document::PartDocument& part,
        const std::vector<Node>& nodes,
        const std::vector<std::string>& authored,
        bool clear_meeting_extensions=false) {
    struct Correction {
        std::array<std::optional<double>,2> setback;
    };
    std::map<std::string,Correction> corrections;
    const auto defaults=document::sheet_metal_defaults(part);
    const auto distance=[](V a,V b) {
        const auto delta=sub(a,b);return std::sqrt(dot(delta,delta));
    };
    const auto cylinder=[](const Node& node) {
        return node.face.surface&&node.face.surface->kind==
            kernel::SurfaceGeometry::Kind::Cylinder;
    };
    for(std::size_t first_index=0;first_index<nodes.size();++first_index) {
        const auto& first_node=nodes[first_index];
        if(!cylinder(first_node))continue;
        if(first_index>=authored.size()||authored[first_index].empty())continue;
        for(std::size_t second_index=first_index+1;
                second_index<nodes.size();++second_index) {
            const auto& second_node=nodes[second_index];
            if(!cylinder(second_node))continue;
            if(second_index>=authored.size()||authored[second_index].empty())continue;
            std::optional<V> meeting;
            double closest=INFINITY;
            for(const auto first:first_node.entry.points)
                for(const auto second:second_node.entry.points) {
                    const double gap=distance(first,second);
                    if(gap<closest) {
                        closest=gap;meeting=mul(add(first,second),.5);
                    }
                }
            if(!meeting||closest>join_tolerance)continue;
            auto* first_feature=part.find_container(authored[first_index]);
            auto* second_feature=part.find_container(authored[second_index]);
            if(!first_feature||!second_feature||
                    first_feature->feature_kind!=document::FeatureKind::Bend||
                    second_feature->feature_kind!=document::FeatureKind::Bend)
                throw std::invalid_argument(
                    "Sheet conversion lost a measured Bend corner.");
            const auto first_sketch=std::ranges::find(part.sketches,
                first_feature->bend.sketch_id,&sketcher::Sketch::id);
            const auto second_sketch=std::ranges::find(part.sketches,
                second_feature->bend.sketch_id,&sketcher::Sketch::id);
            if(first_sketch==part.sketches.end()||
                    second_sketch==part.sketches.end())
                throw std::invalid_argument(
                    "Sheet conversion lost a measured Bend profile.");
            const std::size_t first_end=bend_meeting_end(
                *first_feature,*first_sketch,*meeting,defaults);
            const std::size_t second_end=bend_meeting_end(
                *second_feature,*second_sketch,*meeting,defaults);
            const auto first_extensions=
                document::bend_profile_extensions(*first_feature);
            const auto second_extensions=
                document::bend_profile_extensions(*second_feature);
            const V first_point=bend_inner_end(
                *first_feature,*first_sketch,first_end,defaults);
            const V second_point=bend_inner_end(
                *second_feature,*second_sketch,second_end,defaults);
            constexpr double probe=.25;
            auto first_probe=*first_feature;
            auto first_probe_extensions=first_extensions;
            first_probe_extensions[first_end]-=probe;
            document::set_bend_profile_extensions(first_probe,
                first_probe_extensions[0],first_probe_extensions[1]);
            auto second_probe=*second_feature;
            auto second_probe_extensions=second_extensions;
            second_probe_extensions[second_end]-=probe;
            document::set_bend_profile_extensions(second_probe,
                second_probe_extensions[0],second_probe_extensions[1]);
            const V first_motion=mul(sub(bend_inner_end(first_probe,
                *first_sketch,first_end,defaults),first_point),1/probe);
            const V second_motion=mul(sub(bend_inner_end(second_probe,
                *second_sketch,second_end,defaults),second_point),1/probe);
            // Measure the two basic Bend end profiles, then intersect their
            // actual inward motion lines. This covers drafted and oblique
            // corners with the same geometry as a right-angle 11 - 1 case;
            // no angle-specific clearance formula is involved.
            const V right=sub(second_point,first_point);
            const V first_column=first_motion;
            const V second_column=mul(second_motion,-1);
            const double a=dot(first_column,first_column);
            const double b=dot(first_column,second_column);
            const double c=dot(second_column,second_column);
            const double determinant=a*c-b*b;
            if(determinant<=1e-12)
                throw std::invalid_argument(
                    "Sheet conversion cannot measure parallel Bend corner ends.");
            const double first_rhs=dot(first_column,right);
            const double second_rhs=dot(second_column,right);
            double first_setback=(first_rhs*c-b*second_rhs)/determinant;
            double second_setback=(a*second_rhs-b*first_rhs)/determinant;
            if(first_setback<0&&first_setback>-1e-7)first_setback=0;
            if(second_setback<0&&second_setback>-1e-7)second_setback=0;
            const V first_intersection=add(first_point,
                mul(first_motion,first_setback));
            const V second_intersection=add(second_point,
                mul(second_motion,second_setback));
            const bool native_split=
                first_feature->bend.corner[first_end]&&
                second_feature->bend.corner[second_end];
            if(native_split&&std::isfinite(first_setback)&&
                    std::isfinite(second_setback)&&
                    (first_setback<0||second_setback<0))
                continue;
            if(!std::isfinite(first_setback)||
                    !std::isfinite(second_setback)||first_setback<0||
                    second_setback<0||
                    distance(first_intersection,second_intersection)>1e-5)
                throw std::invalid_argument(
                    "Sheet conversion cannot close the measured Bend corner.");
            const auto store=[&](const std::string& id,std::size_t end,
                    double value) {
                auto& existing=corrections[id].setback[end];
                if(existing&&std::abs(*existing-value)>1e-5)
                    throw std::invalid_argument(
                        "Sheet conversion found conflicting Bend corner ends.");
                existing=value;
            };
            store(authored[first_index],first_end,
                clear_meeting_extensions?
                    std::max(0.,first_extensions[first_end]):
                    first_setback);
            store(authored[second_index],second_end,
                clear_meeting_extensions?
                    std::max(0.,second_extensions[second_end]):
                    second_setback);
        }
    }
    for(const auto& [id,correction]:corrections) {
        auto* feature=part.find_container(id);
        auto extensions=document::bend_profile_extensions(*feature);
        for(std::size_t end=0;end<2;++end)
            if(correction.setback[end]) {
                extensions[end]-=*correction.setback[end];
                if(clear_meeting_extensions) {
                    feature->bend.corner[end]=true;
                    feature->bend.corner_gap=0;
                }
            }
        document::set_bend_profile_extensions(
            *feature,extensions[0],extensions[1]);
    }
    return !corrections.empty();
}

bool apply_remaining_bend_corner_corrections(document::PartDocument& part,
        const std::vector<std::string>& created) {
    // Continuation Bends bridge planar regions after a deleted common corner
    // fillet. They are not represented by a cylinder Node above, so close any
    // remaining zero-gap corner from the authored Bend geometry itself. The
    // measured inner-end motion is the same source of truth as for direct
    // cylinder branches and supports both shortening and extension.
    struct BendEndpoint {
        std::string feature_id;
        std::size_t end{};
        V point;
        V motion;
        double thickness{};
    };
    struct Correction {
        std::array<std::optional<double>,2> setback;
    };
    const auto defaults=document::sheet_metal_defaults(part);
    const auto distance=[](V first,V second) {
        const auto delta=sub(first,second);
        return std::sqrt(dot(delta,delta));
    };
    std::vector<BendEndpoint> endpoints;
    constexpr double probe=.25;
    for(const auto& id:created) {
        const auto* feature=part.find_container(id);
        if(!feature||feature->feature_kind!=document::FeatureKind::Bend||
                feature->bend.corner_gap!=0)
            continue;
        const auto sketch=std::ranges::find(part.sketches,
            feature->bend.sketch_id,&sketcher::Sketch::id);
        if(sketch==part.sketches.end())
            throw std::invalid_argument(
                "Sheet conversion lost a continuation Bend profile.");
        const auto parameters=document::resolved_bend_parameters(
            *feature,defaults);
        const auto extensions=document::bend_profile_extensions(*feature);
        for(std::size_t end=0;end<2;++end) {
            if(!feature->bend.corner[end])continue;
            const V point=bend_inner_end(*feature,*sketch,end,defaults);
            auto probed=*feature;
            auto probed_extensions=extensions;
            probed_extensions[end]-=probe;
            document::set_bend_profile_extensions(probed,
                probed_extensions[0],probed_extensions[1]);
            const V motion=mul(sub(bend_inner_end(
                probed,*sketch,end,defaults),point),1/probe);
            endpoints.push_back({id,end,point,motion,
                parameters.thickness});
        }
    }
    std::vector<std::size_t> nearest(endpoints.size(),endpoints.size());
    std::vector<double> nearest_distance(endpoints.size(),INFINITY);
    for(std::size_t first=0;first<endpoints.size();++first)
        for(std::size_t second=0;second<endpoints.size();++second) {
            if(first==second||endpoints[first].feature_id==
                    endpoints[second].feature_id)
                continue;
            const double gap=distance(
                endpoints[first].point,endpoints[second].point);
            if(gap<nearest_distance[first]) {
                nearest_distance[first]=gap;
                nearest[first]=second;
            }
        }
    std::map<std::string,Correction> residual;
    for(std::size_t first_index=0;first_index<endpoints.size();++first_index) {
        const auto second_index=nearest[first_index];
        if(second_index>=endpoints.size()||second_index<first_index||
                nearest[second_index]!=first_index)
            continue;
        const auto& first=endpoints[first_index];
        const auto& second=endpoints[second_index];
        const double gap=nearest_distance[first_index];
        if(gap<=1e-7)continue;
        const double maximum_gap=2*std::max(
            first.thickness,second.thickness)+join_tolerance;
        if(gap>maximum_gap)continue;
        const V right=sub(second.point,first.point);
        const V first_column=first.motion;
        const V second_column=mul(second.motion,-1);
        const double a=dot(first_column,first_column);
        const double b=dot(first_column,second_column);
        const double c=dot(second_column,second_column);
        const double determinant=a*c-b*b;
        if(determinant<=1e-12)continue;
        const double first_rhs=dot(first_column,right);
        const double second_rhs=dot(second_column,right);
        const double first_setback=(first_rhs*c-b*second_rhs)/determinant;
        const double second_setback=(a*second_rhs-b*first_rhs)/determinant;
        const V first_intersection=add(first.point,
            mul(first.motion,first_setback));
        const V second_intersection=add(second.point,
            mul(second.motion,second_setback));
        if(!std::isfinite(first_setback)||!std::isfinite(second_setback)||
                distance(first_intersection,second_intersection)>1e-5)
            continue;
        const auto store=[&](const BendEndpoint& endpoint,double value) {
            auto& existing=residual[endpoint.feature_id].setback[endpoint.end];
            if(existing&&std::abs(*existing-value)>1e-5)
                throw std::invalid_argument(
                    "Sheet conversion found conflicting continuation Bend corners.");
            existing=value;
        };
        store(first,first_setback);
        store(second,second_setback);
    }
    for(const auto& [id,correction]:residual) {
        auto* feature=part.find_container(id);
        auto extensions=document::bend_profile_extensions(*feature);
        for(std::size_t end=0;end<2;++end)
            if(correction.setback[end])
                extensions[end]-=*correction.setback[end];
        document::set_bend_profile_extensions(
            *feature,extensions[0],extensions[1]);
    }
    return !residual.empty();
}
}
double suggest_sheet_thickness(const kernel::ViewerMesh& mesh,const Face& selected) {
    const auto available=faces(mesh);const auto it=available.find(key(selected));
    return it==available.end()?0:planar_pair_distance(available,it->second);
}
double suggest_sheet_thickness(const Workspace& live,const std::string& id,
        const kernel::ViewerMesh& mesh,const Face& selected) {
    const auto available=faces(mesh);std::vector<Face> planar;
    for(const auto& [identity,face]:available)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane)
        planar.push_back(face);
    const auto areas=measurement_face_areas(live,id,planar);
    kernel::ViewerMesh analysis;
    for(std::size_t i=0;i<planar.size();++i) {
        planar[i].measured_area=areas[i];analysis.triangle_references.push_back(std::move(planar[i]));
    }
    return suggest_sheet_thickness(analysis,selected);
}
SheetBodyConversion prepare_sheet_from_body(const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,const Face& selected,
        double thickness,const kernel::OcctKernel& kernel,
        bool thickness_override,std::vector<Face> included_faces,
        std::vector<std::string> rip_sketches) {
    SheetConversionAuthoringScope authoring_scope;
    const auto* target=source.body_history.find(source.body_history.active_body_id());
    if(!target||target->derived_copy)throw std::invalid_argument("Sheet from Body requires an editable active Body.");
    if(!thickness_override)
        thickness=document::sheet_metal_defaults(source).
            thickness_mm.value_or(1.);
    if(!(thickness>=.001)||thickness>1000000||!std::isfinite(thickness))throw std::invalid_argument("Sheet thickness must be between 0.001 and 1000000 mm.");
    if(cache.empty())throw std::invalid_argument("Sheet from Body requires a calculated source body.");
    const auto source_owner=source.body_owner_for_object(selected.owner_id);
    const bool replaces_body=!target->entries.empty();
    if(!source_owner||(replaces_body&&source_owner->scope.id!=target->scope.id))throw std::invalid_argument("Select a planar face of the active Body.");
    std::set<std::pair<std::string,std::string>> selected_face_ids;
    selected_face_ids.emplace(selected.owner_id,selected.semantic_key);
    for(const auto& face:included_faces) {
        const auto owner=source.body_owner_for_object(face.owner_id);
        if(!owner||owner->scope.id!=source_owner->scope.id||
                face.instance_path!=selected.instance_path||
                !face.surface||
                (face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane&&
                 face.surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder))
            throw std::invalid_argument(
                "Included Sheet-from-Body faces must belong to the selected source Body.");
        if(!selected_face_ids.emplace(face.owner_id,face.semantic_key).second)
            throw std::invalid_argument(
                "Sheet-from-Body source faces must be unique.");
    }
    std::vector<const sketcher::Sketch*> rip_sources;
    std::set<std::string> unique_rips;
    for(const auto& id:rip_sketches) {
        if(id.empty()||!unique_rips.insert(id).second)
            throw std::invalid_argument("RIP Sketch references must be unique.");
        const auto sketch=std::ranges::find(source.sketches,id,
            &sketcher::Sketch::id);
        if(sketch==source.sketches.end()||sketch->suppressed)
            throw std::invalid_argument(
                "A selected RIP Sketch is missing or suppressed.");
        const auto owner=source.body_owner_for_object(
            sketch->owner_container_id);
        if(!owner||owner->scope.id!=source_owner->scope.id)
            throw std::invalid_argument(
                "RIP Sketches must precede the conversion in the selected source Body.");
        const auto entry=std::ranges::find(owner->entries,
            sketch->owner_container_id,&document::PartHistoryEntry::id);
        if(entry==owner->entries.end()||
                static_cast<std::size_t>(entry-owner->entries.begin())>=
                    owner->cursor)
            throw std::invalid_argument(
                "RIP Sketches must precede the conversion in the selected source Body.");
        rip_sources.push_back(&*sketch);
    }
    const auto authored_edge=[&](const kernel::EdgeReference& reference)
            ->const kernel::ViewerEdge* {
        const auto find=[&](const auto& edges)->const kernel::ViewerEdge* {
            const auto edge=std::ranges::find(edges,reference,
                &kernel::ViewerEdge::reference);
            return edge!=edges.end()&&edge->points.size()>=2?&*edge:nullptr;
        };
        for(const auto& boundary:cache) {
            if(const auto* edge=find(boundary.mesh.edges))return edge;
            if(const auto* edge=find(
                    boundary.mesh.original_references.edges))return edge;
            const auto body=boundary.body_outputs.find(source_owner->scope.id);
            if(body==boundary.body_outputs.end())continue;
            if(const auto* edge=find(body->second->mesh.edges))return edge;
            if(const auto* edge=find(
                    body->second->mesh.original_references.edges))return edge;
        }
        return nullptr;
    };
    for(const auto& entry:source_owner->entries) {
        const auto* feature=source.find_container(entry.id);
        if(!feature||feature->feature_kind!=document::FeatureKind::Fillet)
            continue;
        for(const auto& route:feature->edge_treatment.routes) {
            if(route.size()<3)continue;
            std::vector<const kernel::ViewerEdge*> edges;
            for(const auto& reference:route)
                if(const auto* edge=authored_edge(reference))
                    edges.push_back(edge);
            if(edges.size()!=route.size())continue;
            const auto distance=[](V first,V second) {
                const auto delta=sub(first,second);
                return std::sqrt(dot(delta,delta));
            };
            for(const auto* seed_edge:edges)
                for(const auto point:{seed_edge->points.front(),
                        seed_edge->points.back()}) {
                    const auto meetings=std::ranges::count_if(edges,
                        [&](const auto* edge) {
                            return distance(point,edge->points.front())<=
                                    join_tolerance||
                                distance(point,edge->points.back())<=
                                    join_tolerance;
                        });
                    if(meetings>=3)throw std::invalid_argument(
                        "Sheet conversion cannot reproduce this surface transition.");
                }
        }
    }
    const bool imported_step_source=std::ranges::any_of(
        source_owner->entries.begin(),
        std::next(source_owner->entries.begin(),
            static_cast<std::ptrdiff_t>(std::min(source_owner->cursor,
                source_owner->entries.size()))),
        [&](const auto& entry) {
            const auto* feature=source.find_container(entry.id);
            return feature&&feature->feature_kind==
                document::FeatureKind::ImportedStep;
        });
    // A native solid (Extrusion/Feature/Fillet...) has no manufacturing
    // clearance contract, so adjacent Bend ends are measured after their
    // basic profiles exist. STEP geometry represents an already finished
    // sheet supplied by another system; preserve its endpoints verbatim.
    const auto& order=source.body_history.order();
    if(!replaces_body&&std::ranges::find(order,source_owner->scope.id)>=std::ranges::find(order,target->scope.id))
        throw std::invalid_argument("Source Body must precede the active Body.");
    const std::vector<kernel::BodyResult>* source_cache=&cache;
    std::vector<kernel::BodyResult> ripped_cache;
    if(!rip_sources.empty()) {
        ripped_cache=calculate_ripped_source(source,cache,
            source_owner->scope.id,rip_sources,kernel);
        source_cache=&ripped_cache;
    }
    const auto input=source_cache->back().body_outputs.find(
        source_owner->scope.id);
    if(input==source_cache->back().body_outputs.end()||
            !input->second->calculation_errors.empty())
        throw std::invalid_argument(
            "Sheet from Body requires a calculated source body.");
    // Body outputs already include their Body placement. Convert that world
    // packet once to the active Body's local coordinates through the public API.
    auto mesh=input->second->mesh;
    auto local_context=sketcher::Sketch::create_default();local_context.owner_container_id=target->scope.id;
    kernel::ViewerReferenceGeometry packet{std::move(mesh.vertices),std::move(mesh.triangles),std::move(mesh.triangle_references),std::move(mesh.edges),std::move(mesh.points),std::move(mesh.axes)};
    packet=source.sketch_reference_geometry_for(local_context,std::move(packet));
    mesh.vertices=std::move(packet.vertices);mesh.triangles=std::move(packet.triangles);mesh.triangle_references=std::move(packet.triangle_references);
    mesh.edges=std::move(packet.edges);mesh.points=std::move(packet.points);mesh.axes=std::move(packet.axes);
    mesh.original_references=source.sketch_reference_geometry_for(local_context,std::move(mesh.original_references));
    auto local_selected=selected;
    if(selected.surface) {
        kernel::ViewerReferenceGeometry selection;
        selection.triangle_references.push_back(selected);
        selection=source.sketch_reference_geometry_for(
            local_context,std::move(selection));
        if(!selection.triangle_references.empty())
            local_selected=selection.triangle_references.front();
    }
    std::vector<const Face*> root_candidates;
    const auto collect_root=[&](const auto& candidates) {
        for(const auto& candidate:candidates)
            if(same_or_descendant_reference(candidate,local_selected)&&
                    candidate.surface)
                root_candidates.push_back(&candidate);
    };
    collect_root(mesh.original_references.triangle_references);
    collect_root(mesh.triangle_references);
    const auto available=faces(mesh);
    const auto fallback_root=available.find(key(selected));
    const Face* root=nullptr;
    if(!root_candidates.empty())root=*std::ranges::max_element(
        root_candidates,{},[](const Face* face) {
            return face->measured_area.value_or(0.);
        });
    else if(fallback_root!=available.end())root=&fallback_root->second;
    if(!root||!root->surface||
            root->surface->kind!=kernel::SurfaceGeometry::Kind::Plane)
        throw std::invalid_argument("Select a calculated planar source face.");
    // Without RIP the selected skin has one ordinary material root. RIP can
    // partition that same persisted source face into several independent
    // material branches. Keep the largest fragment first for stable ordinary
    // behavior, then author every other exact descendant once.
    std::vector<const Face*> roots{root};
    if(!rip_sources.empty()) {
        roots.clear();
        // The original-reference packet deliberately retains the unsplit
        // authored face for inspection. Only calculated triangle faces are
        // actual material branches after RIP and may become root Flats.
        for(const auto& candidate:mesh.triangle_references) {
            if(!same_or_descendant_reference(candidate,local_selected)||
                    !candidate.surface||candidate.surface->kind!=
                    kernel::SurfaceGeometry::Kind::Plane||
                    std::ranges::any_of(roots,[&](const Face* existing) {
                        return key(*existing)==key(candidate)&&
                            existing->instance_path==candidate.instance_path;
                    }))continue;
            roots.push_back(&candidate);
        }
        std::ranges::sort(roots,[](const Face* first,
                const Face* second) {
            const double first_area=first->measured_area.value_or(0.);
            const double second_area=second->measured_area.value_or(0.);
            if(std::abs(first_area-second_area)>1e-9)
                return first_area>second_area;
            return key(*first)<key(*second);
        });
        if(roots.empty())throw std::invalid_argument(
            "Sheet RIP did not produce a material region on the selected face.");
        root=roots.front();
    }
    const double measured_source_thickness=
        planar_pair_distance(available,*root);
    const double source_sheet_thickness=imported_step_source&&
            std::isfinite(measured_source_thickness)&&
            measured_source_thickness>.001?
        measured_source_thickness:thickness;
    std::vector<Node> nodes;
    std::vector<std::size_t> root_indices;
    for(const auto* branch_root:roots) {
        auto branch=skin(mesh,*branch_root);
        if(branch.empty())continue;
        const auto offset=nodes.size();
        root_indices.push_back(offset);
        for(auto& node:branch) {
            if(node.parent)*node.parent+=offset;
            nodes.push_back(std::move(node));
        }
    }
    if(root_indices.empty())throw std::invalid_argument(
        "Sheet conversion found no material branch on the selected face.");
    if(!included_faces.empty()) {
        const auto same_sheet_region=[&](const Face& candidate,
                const Face& requested) {
            if(same_reference_lineage(candidate,requested))return true;
            if(candidate.owner_id!=requested.owner_id||
                    candidate.instance_path!=requested.instance_path||
                    !candidate.surface||!requested.surface||
                    candidate.surface->kind!=requested.surface->kind)
                return false;
            const auto& first=*candidate.surface;
            const auto& second=*requested.surface;
            const auto first_axis=unit(first.axis);
            const auto second_axis=unit(second.axis);
            if(std::abs(dot(first_axis,second_axis))<1-1e-8)
                return false;
            const double maximum_separation=source_sheet_thickness+
                join_tolerance;
            if(first.kind==kernel::SurfaceGeometry::Kind::Plane)
                return std::abs(dot(sub(second.origin,first.origin),
                    first_axis))<=maximum_separation;
            const auto axis_offset=cross(sub(second.origin,first.origin),
                first_axis);
            if(std::sqrt(dot(axis_offset,axis_offset))>join_tolerance||
                    std::abs(first.radius-second.radius)>
                        maximum_separation)
                return false;
            const double second_origin=dot(sub(second.origin,first.origin),
                first_axis);
            const double direction=dot(first_axis,second_axis);
            const auto second_min=second_origin+direction*
                second.axial_min;
            const auto second_max=second_origin+direction*
                second.axial_max;
            const auto low=std::min(second_min,second_max);
            const auto high=std::max(second_min,second_max);
            return high>=first.axial_min-join_tolerance&&
                low<=first.axial_max+join_tolerance;
        };
        std::set<std::size_t> retained(root_indices.begin(),
            root_indices.end());
        for(const auto& selected_face:included_faces) {
            bool found{};
            for(std::size_t index=0;index<nodes.size();++index)
                if(same_sheet_region(nodes[index].face,
                        selected_face)) {
                    found=true;
                    for(auto current=std::optional<std::size_t>{index};current;
                            current=nodes[*current].parent)
                        retained.insert(*current);
                    if(nodes[index].face.surface&&
                            nodes[index].face.surface->kind==
                                kernel::SurfaceGeometry::Kind::Cylinder)
                        for(std::size_t child=0;child<nodes.size();++child)
                            if(nodes[child].parent==index&&
                                    nodes[child].face.surface&&
                                    nodes[child].face.surface->kind==
                                        kernel::SurfaceGeometry::Kind::Plane)
                                retained.insert(child);
                }
            if(!found)throw std::invalid_argument(
                "An included face is not connected to the selected sheet region.");
        }
        std::vector<Node> limited;
        std::map<std::size_t,std::size_t> remap;
        for(std::size_t index=0;index<nodes.size();++index)
            if(retained.contains(index)) {
                remap[index]=limited.size();
                auto node=nodes[index];
                if(node.parent)node.parent=remap.at(*node.parent);
                limited.push_back(std::move(node));
            }
        for(auto& root_index:root_indices)root_index=remap.at(root_index);
        nodes=std::move(limited);
    }
    if(imported_step_source)
        static_cast<void>(append_imported_compound_bends(
            mesh,nodes,source_sheet_thickness));
    const auto continuations=imported_step_source?
        std::vector<ContinuationBend>{}:
        infer_corner_continuations(mesh,nodes);
    std::map<std::size_t,std::vector<std::size_t>> continuations_by_target;
    for(std::size_t index=0;index<continuations.size();++index)
        continuations_by_target[continuations[index].planes[1]].
            push_back(index);
    const auto incoming_edges=[&](std::size_t plane) {
        std::vector<kernel::ViewerEdge> result{nodes[plane].entry};
        if(const auto found=continuations_by_target.find(plane);
                found!=continuations_by_target.end())
            for(const auto index:found->second)
                result.push_back(continuations[index].edges[1]);
        return result;
    };
    SheetBodyConversion result;result.document=source;result.calculated=cache;
    // Imported STEP sheet reconstruction remains an ordinary editable Flat /
    // Bend sequence.  It needs the non-geometric replacement boundary only
    // when replacing material in the same Body, and never owns/nests the
    // generated features.  Native parametric solid conversion is associative
    // in both same-Body and cross-Body workflows.
    const bool associative_source=!imported_step_source;
    if(replaces_body||associative_source) {
        auto conversion=
            document::PartDocument::create_sheet_from_body_container();
        conversion.sheet_from_body.source_body_id=source_owner->scope.id;
        if(associative_source) {
            conversion.sheet_from_body.source_face={
                persistent_face_reference(selected),
                document::SheetFromBodyFaceRole::Skin,
                selected.surface&&selected.surface->reversed?-1:1,
                surface_signature(selected)};
            for(const auto& face:included_faces)
                conversion.sheet_from_body.included_faces.push_back({
                    persistent_face_reference(face),
                    document::SheetFromBodyFaceRole::Skin,
                    face.surface&&face.surface->reversed?-1:1,
                    surface_signature(face)});
            conversion.sheet_from_body.rip_sketches=std::move(rip_sketches);
        }
        conversion.sheet_from_body.thickness=thickness;
        conversion.sheet_from_body.thickness_override=thickness_override;
        conversion.sheet_from_body.replaces_body=replaces_body;
        result.container_id=conversion.id;
        result.document.insert_history_entry(
            document::PartHistoryKind::Feature,conversion.id);
        result.document.history.push_back(std::move(conversion));
    }
    if(associative_source&&!replaces_body) {
        auto target_body=*result.document.body_history.find(target->scope.id);
        if(std::ranges::find(target_body.dependencies,
                source_owner->scope.id)==target_body.dependencies.end())
            target_body.dependencies.push_back(source_owner->scope.id);
        result.document.body_history.update_body(std::move(target_body));
    }
    // Author one ordinary editable root Flat per material branch. The source
    // solid remains the parametric driver; the conversion boundary excludes
    // that solid from the later body result. Adjacent RIP roots may fuse back
    // into one body, but their authored profiles and source regions stay
    // independent for regeneration and later line bending.
    std::vector<std::string> root_features(nodes.size());
    for(const auto root_index:root_indices) {
        const auto& branch_root=nodes[root_index].face;
        auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_sketch_container();
        feature.feature_kind=document::FeatureKind::Flat;
        feature.name="Tabule";
        feature.flat.sketch_id=sketch.id;
        feature.flat.thickness_override=thickness_override;
        feature.flat.thickness=thickness;
        feature.flat.direction=document::ExtrusionDirection::Reverse;
        sketch.owner_container_id=feature.id;
        sketch.name=feature.name;
        const auto frame=root_sheet_frame(source,local_context,mesh,
            branch_root);
        numeric_frame(feature,sketch,frame.origin,frame.x,frame.y,
            frame.normal);
        outline(sketch,mesh,branch_root);
        root_features[root_index]=feature.id;
        result.document.insert_history_entry(
            document::PartHistoryKind::Feature,feature.id);
        result.created.push_back(feature.id);
        result.document.history.push_back(std::move(feature));
        result.document.sketches.push_back(std::move(sketch));
        if(associative_source)
            append_source_binding(result.document,result.container_id,
                source_region_binding(mesh,result.created.back(),branch_root,
                    thickness,true,"root"));
    }
    if(!result.container_id.empty())
        result.document.find_container(result.container_id)->
            sheet_from_body.generated_features=result.created;
    result.document.resolve_constructions(construction_reference_source_geometry(cache));
    try {
        result.calculated=kernel.evaluate_history_incremental(
            result.document.kernel_operations(),cache);
    } catch(const std::exception& error) {
        std::string profile;
        for(const auto& segment:result.document.sketches.back().segments) {
            const auto* a=result.document.sketches.back().find_point(
                segment.first_point_id);
            const auto* b=result.document.sketches.back().find_point(
                segment.second_point_id);
            if(a&&b)profile+=" ["+std::to_string(a->x)+","+
                std::to_string(a->y)+" -> "+std::to_string(b->x)+","+
                std::to_string(b->y)+"]";
        }
        throw std::invalid_argument("Sheet conversion root Flat failed ("+
            std::to_string(result.document.sketches.back().segments.size())+
            " segments, "+
            std::to_string(result.document.sketches.back().points.size())+
            " points): "+error.what()+profile);
    }
    if(imported_step_source) {
        // A finished imported sheet already carries its manufacturing seams.
        // Rebuild it branch-by-branch with the ordinary native contract:
        // Flat -> Bend -> the Bend's output End -> Flat.  The general solid
        // conversion below deliberately remains a separate algorithm because
        // it must infer clearances and omitted corner continuations that do
        // not exist in a finished sheet model.
        std::vector<std::string> imported_authored(nodes.size());
        for(const auto root_index:root_indices)
            imported_authored[root_index]=root_features[root_index];
        std::vector<std::size_t> imported_order;
        std::vector<std::size_t> pending=root_indices;
        while(!pending.empty()) {
            const auto parent=pending.back();
            pending.pop_back();
            if(nodes[parent].parent)imported_order.push_back(parent);
            std::vector<std::size_t> children;
            for(std::size_t index=1;index<nodes.size();++index)
                if(nodes[index].parent&&*nodes[index].parent==parent)
                    children.push_back(index);
            const auto entry_length_squared=[&](std::size_t index) {
                const auto& points=nodes[index].entry.points;
                if(points.size()<2)return 0.;
                const auto delta=sub(points.back(),points.front());
                return dot(delta,delta);
            };
            // Complete the long manufacturing walls before their short end
            // tabs.  STEP exporters can represent one tab corner by two
            // neighbouring rounds; adding that closed end first leaves a
            // transient overlap when the full-length sibling Flat is fused.
            // The stack pops its last item, hence ascending length and reverse
            // identity order here produce longest-first stable traversal.
            std::ranges::sort(children,[&](std::size_t first,
                    std::size_t second) {
                const double first_length=entry_length_squared(first);
                const double second_length=entry_length_squared(second);
                if(std::abs(first_length-second_length)>1e-12)
                    return first_length<second_length;
                return first>second;
            });
            pending.insert(pending.end(),children.begin(),children.end());
        }
        const auto edge_distance=[](const kernel::ViewerEdge& first,
                const kernel::ViewerEdge& second) {
            const auto distance=[](V a,V b) {
                const auto delta=sub(a,b);
                return std::sqrt(dot(delta,delta));
            };
            if(first.points.empty()||second.points.empty())
                return std::numeric_limits<double>::infinity();
            return std::min(
                std::max(distance(first.points.front(),second.points.front()),
                    distance(first.points.back(),second.points.back())),
                std::max(distance(first.points.front(),second.points.back()),
                    distance(first.points.back(),second.points.front())));
        };
        for(const auto index:imported_order) {
            const auto& node=nodes[index];
            if(!node.parent||*node.parent>=imported_authored.size()||
                    imported_authored[*node.parent].empty()) {
                throw std::invalid_argument(
                    "Sheet conversion cannot reproduce every connected face.");
            }
            const auto& parent=imported_authored[*node.parent];
            const auto& surface=*node.face.surface;
            const bool bend=surface.kind==
                kernel::SurfaceGeometry::Kind::Cylinder;
            const auto* exit_edge=bend?bend_exit_edge(nodes,index):nullptr;
            if(bend&&!exit_edge) {
                continue;
            }

            const auto geometry=construction_reference_source_geometry(
                result.calculated);
            const auto local_geometry=source.sketch_reference_geometry_for(
                local_context,geometry);
            auto a=node.entry.points.front();
            auto b=node.entry.points.back();
            if(bend&&surface.reversed) {
                const auto shift=mul(normal(node.face,a),-thickness);
                a=add(a,shift);b=add(b,shift);
            }
            const auto distance=[](V first,V second) {
                const auto delta=sub(first,second);
                return std::sqrt(dot(delta,delta));
            };
            const kernel::ViewerEdge* attachment=nullptr;
            double attachment_error=INFINITY;
            if(bend) {
                // A Bend starts on the exact source-face boundary represented
                // by its parent native Flat.
                for(const auto& edge:local_geometry.edges) {
                    if(edge.reference.owner_id!=parent||!straight(edge))
                        continue;
                    try {
                        static_cast<void>(
                            document::bend_sheet_references(edge));
                    } catch(const std::invalid_argument&) {continue;}
                    const double error=std::min(
                        std::max(distance(a,edge.points.front()),
                            distance(b,edge.points.back())),
                        std::max(distance(b,edge.points.front()),
                            distance(a,edge.points.back())));
                    if(error<attachment_error) {
                        attachment_error=error;
                        attachment=&edge;
                    }
                }
            } else {
                // Do not rediscover a merely similar edge from the STEP face.
                // The following Flat is owned by the explicit output End of
                // its newly authored native Bend, including both End vertices.
                const auto* parent_feature=result.document.find_container(
                    parent);
                if(!parent_feature||parent_feature->feature_kind!=
                        document::FeatureKind::Bend)
                    throw std::invalid_argument(
                        "Sheet conversion cannot reproduce every connected face.");
                const auto parent_sketch=std::ranges::find(
                    result.document.sketches,parent_feature->bend.sketch_id,
                    &sketcher::Sketch::id);
                if(parent_sketch==result.document.sketches.end())
                    throw std::invalid_argument(
                        "Sheet conversion cannot reproduce every connected face.");
                const auto preview=document::bend_preview(*parent_feature,
                    *parent_sketch,
                    document::sheet_metal_defaults(result.document));
                const auto& output_end=bend_preview_edge(
                    preview,"outer-end");
                for(const auto& edge:local_geometry.edges) {
                    if(edge.reference.owner_id!=parent||!straight(edge))
                        continue;
                    try {
                        static_cast<void>(
                            document::flat_sheet_references(edge));
                    } catch(const std::invalid_argument&) {continue;}
                    const double error=edge_distance(edge,output_end);
                    if(error<attachment_error) {
                        attachment_error=error;
                        attachment=&edge;
                    }
                }
            }
            if(!attachment||attachment_error>join_tolerance) {
                throw std::invalid_argument(
                    "Sheet conversion cannot reproduce every connected face.");
            }
            const auto attachment_anchor=[&]() {
                if(attachment->edge_treatment_endpoint_references.size()!=2||
                        attachment->points.size()<2)
                    return kernel::VertexReference{};
                const auto target=node.entry.points.front();
                const auto first_delta=sub(attachment->points.front(),target);
                const auto last_delta=sub(attachment->points.back(),target);
                return attachment->edge_treatment_endpoint_references[
                    dot(last_delta,last_delta)<dot(first_delta,first_delta)?1:0];
            }();

            auto next=result.document;
            auto profile=sketcher::Sketch::create_default();
            auto added=document::PartDocument::create_sketch_container();
            profile.owner_container_id=added.id;
            added.feature_kind=bend?document::FeatureKind::Bend:
                document::FeatureKind::Flat;
            added.name=bend?"Ohyb":"Tabule";
            profile.name=added.name;
            if(bend) {
                const auto incoming=normal(node.face,
                    node.entry.points.front());
                const auto outgoing=normal(node.face,
                    exit_edge->points.front());
                added.bend.angle_degrees=std::acos(std::clamp(
                    dot(incoming,outgoing),-1.,1.))*180/std::numbers::pi;
                added.bend.radius=surface.radius-
                    (surface.reversed?0:thickness);
                if(std::abs(added.bend.radius)<=1e-7)
                    added.bend.radius=0;
                if(added.bend.radius<0||
                        added.bend.angle_degrees<.001)
                    throw std::invalid_argument(
                        "Sheet conversion cannot reproduce this surface transition.");
                added.bend.sketch_id=profile.id;
                added.bend.sheet_attachment=true;
                added.bend.thickness_override=thickness_override;
                added.bend.thickness=thickness;
                document::initialize_bend_start_profile(profile,
                    distance(a,b));
                added.placement.references=
                    document::bend_sheet_references(
                        *attachment,attachment_anchor);
            } else {
                added.flat.sketch_id=profile.id;
                added.flat.sheet_attachment=true;
                added.flat.thickness_override=thickness_override;
                added.flat.thickness=thickness;
                added.flat.direction=document::ExtrusionDirection::Reverse;
                added.placement.references=
                    document::flat_sheet_references(
                        *attachment,attachment_anchor);
            }
            next.insert_history_entry(document::PartHistoryKind::Feature,
                added.id);
            next.history.push_back(added);
            next.sketches.push_back(profile);
            if(!result.container_id.empty())
                next.find_container(result.container_id)->
                    sheet_from_body.generated_features.push_back(added.id);
            next.resolve_constructions(geometry);
            auto& placed=next.sketches.back();
            if(!bend) {
                if(std::abs(dot(placed.resolved_normal,surface.axis))<
                        1-1e-6)
                    throw std::invalid_argument(
                        "Sheet conversion cannot reproduce this surface transition.");
                outline(placed,mesh,node.face);
                bind_flat_outline_to_attachment(placed,0,false);
            } else {
                document::prepare_bend_sketches(next.history.back(),placed,
                    document::sheet_metal_defaults(next));
                const auto end=sketcher::Sketch::from_serialized(
                    next.history.back().bend.auxiliary_sketches[1]);
                const auto first=end.local_point(exit_edge->points.front());
                const auto last=end.local_point(exit_edge->points.back());
                const auto* start_reference=end.find_point(
                    end.id+":reference:first");
                const auto* end_reference=end.find_point(
                    end.id+":reference:last");
                const double source_start=std::min(first[0],last[0]);
                const double source_end=std::max(first[0],last[0]);
                for(const auto& other:nodes) {
                    if(&other==&node||!other.face.surface||
                            other.face.surface->kind!=
                                kernel::SurfaceGeometry::Kind::Cylinder)
                        continue;
                    for(const auto point:node.entry.points)
                        for(const auto other_point:other.entry.points) {
                            if(distance(point,other_point)>join_tolerance)
                                continue;
                            const auto local=end.local_point(point);
                            const std::size_t endpoint=
                                std::abs(local[0]-source_start)<=
                                    std::abs(local[0]-source_end)?0:1;
                            next.history.back().bend.corner[endpoint]=true;
                            next.history.back().bend.corner_gap=0;
                        }
                }
                document::set_bend_profile_extensions(next.history.back(),
                    start_reference->x-source_start,
                    source_end-end_reference->x);
                document::prepare_bend_sketches(next.history.back(),placed,
                    document::sheet_metal_defaults(next));
            }
            PartCalculationPolicy policy;
            policy.reject_errors=true;
            auto calculated=calculate_part_with_resolved_references(
                kernel,next,&result.calculated,policy);
            result.document=std::move(next);
            result.calculated=std::move(calculated);
            result.created.push_back(added.id);
            imported_authored[index]=added.id;
        }
        result.document.validate_sheet_from_body();
        return result;
    }
    std::vector<std::string> authored(nodes.size());
    for(const auto root_index:root_indices)
        authored[root_index]=root_features[root_index];
    std::vector<std::size_t> depth(nodes.size());
    std::size_t maximum_depth{};
    for(std::size_t i=0;i<nodes.size();++i) {
        if(!nodes[i].parent)continue;
        if(*nodes[i].parent>=i)
            throw std::invalid_argument(
                "Sheet conversion found an invalid branch order.");
        depth[i]=depth[*nodes[i].parent]+1;
        maximum_depth=std::max(maximum_depth,depth[i]);
    }
    bool imported_corner_fallback_used{};
    for(std::size_t current_depth=1;current_depth<=maximum_depth;
            ++current_depth) {
        const auto wave_geometry=construction_reference_source_geometry(
            result.calculated);
        const auto local_geometry=source.sketch_reference_geometry_for(
            local_context,wave_geometry);
        auto wave_document=result.document;
        bool wave_changed{};
        const auto wave_created_size=result.created.size();
        const auto wave_skipped=result.skipped;
    for(std::size_t i=1;i<nodes.size();++i) {
        if(depth[i]!=current_depth)continue;
        const auto& node=nodes[i];
        const auto skip=[&](const char*) {
            ++result.skipped;
        };
        if(!node.parent||*node.parent>=authored.size()||
                authored[*node.parent].empty()){skip("parent");continue;}
        const auto& parent=authored[*node.parent];
        const auto& surface=*node.face.surface;
        const bool bend=surface.kind==kernel::SurfaceGeometry::Kind::Cylinder;
        const auto* exit_edge=bend?bend_exit_edge(nodes,i):nullptr;
        // A Bend is useful only when it leads to a newly authored planar
        // wall. In a closed skin the visited-face guard makes the returning
        // cylinder terminal; omitting it creates the required finite seam.
        // An open terminal cylinder is omitted for the same reason: a native
        // sheet history must never end with an unsupported Bend.
        if(bend&&!exit_edge)continue;
        auto a=node.entry.points.front(),b=node.entry.points.back();
        if(bend&&surface.reversed){const auto shift=mul(normal(node.face,a),-thickness);a=add(a,shift);b=add(b,shift);}
        const auto distance=[](V p,V q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z);};
        const auto source_tangent=unit(sub(b,a));
        const kernel::ViewerEdge* attachment=nullptr;double best=INFINITY;
        for(const auto& edge:local_geometry.edges) {
            if(edge.reference.owner_id!=parent||!straight(edge))continue;
            try {if(bend)static_cast<void>(document::bend_sheet_references(edge));else static_cast<void>(document::flat_sheet_references(edge));}
            catch(const std::invalid_argument&){continue;}
            const auto mismatch=[&](V p,V q) {
                if(bend)return distance(p,q);
                const auto n=normal(node.face,p),delta=sub(q,p);const double offset=dot(delta,n);
                const auto in_plane=sub(delta,mul(n,offset));
                const double along=dot(in_plane,source_tangent);
                const auto across=sub(in_plane,mul(source_tangent,along));
                return std::max({std::sqrt(dot(across,across)),
                    std::min(std::abs(offset),
                        std::abs(std::abs(offset)-thickness)),
                    std::max(0.,std::abs(along)-
                        (imported_corner_fallback_used?thickness:0.))});
            };
            const double error=std::min(std::max(mismatch(a,edge.points.front()),mismatch(b,edge.points.back())),
                std::max(mismatch(b,edge.points.front()),mismatch(a,edge.points.back())));
            if(error<best){best=error;attachment=&edge;}
        }
        // Reuse the native parent's exact joining edge within the agreed
        // 0.05 mm sheet conversion tolerance; never widen general CAD snapping.
        if(!attachment||best>join_tolerance){skip("attachment");continue;}
        auto next=wave_document;auto profile=sketcher::Sketch::create_default();
        auto added=document::PartDocument::create_sketch_container();profile.owner_container_id=added.id;
        added.feature_kind=bend?document::FeatureKind::Bend:document::FeatureKind::Flat;
        added.name=bend?"Ohyb":"Tabule";profile.name=added.name;
        if(bend) {
            const auto incoming=normal(node.face,node.entry.points.front()),outgoing=normal(node.face,exit_edge->points.front());
            added.bend.angle_degrees=std::acos(std::clamp(dot(incoming,outgoing),-1.,1.))*180/std::numbers::pi;
            added.bend.radius=surface.radius-(surface.reversed?0:thickness);
            // The source may expose an outer radius exactly equal to the
            // requested material thickness. That is the legitimate native
            // zero-inside-radius Bend, not a missing transition. Clamp only
            // numerical noise around zero; a materially negative radius is
            // still unreproducible.
            if(std::abs(added.bend.radius)<=1e-7)added.bend.radius=0;
            if(added.bend.radius<0||added.bend.angle_degrees<.001){skip("bend parameters");continue;}
            added.bend.sketch_id=profile.id;added.bend.sheet_attachment=true;
            added.bend.thickness_override=thickness_override;
            added.bend.thickness=thickness;
            document::initialize_bend_start_profile(profile,distance(a,b));
            added.placement.references=document::bend_sheet_references(*attachment);
        }else {
            added.flat.sketch_id=profile.id;added.flat.sheet_attachment=true;
            added.flat.thickness_override=thickness_override;
            added.flat.thickness=thickness;
            added.flat.direction=document::ExtrusionDirection::Reverse;
            added.placement.references=document::flat_sheet_references(*attachment);
        }
        next.insert_history_entry(document::PartHistoryKind::Feature,added.id);
        next.history.push_back(added);next.sketches.push_back(profile);
        next.find_container(result.container_id)->
            sheet_from_body.generated_features.push_back(added.id);
        std::vector<std::pair<const kernel::ViewerEdge*,
            document::SheetFromBodyEdgeRole>> source_edges{
            {&node.entry,document::SheetFromBodyEdgeRole::Entry}};
        if(exit_edge)source_edges.push_back(
            {exit_edge,document::SheetFromBodyEdgeRole::Exit});
        append_source_binding(next,result.container_id,
            source_region_binding(mesh,added.id,node.face,thickness,
                false,"node:"+std::to_string(i),source_edges));
        try {
            next.resolve_constructions(wave_geometry);
        } catch(const std::exception& error) {
            throw std::invalid_argument("Sheet conversion node "+
                std::to_string(i)+" at wave "+
                std::to_string(current_depth)+" failed to resolve: "+
                error.what());
        }
        auto& placed=next.sketches.back();
        if(!bend) {
            if(std::abs(dot(placed.resolved_normal,surface.axis))<1-1e-6){skip("flat frame");continue;}
            if(continuations_by_target.contains(i))
                split_outline(placed,mesh,node.face,incoming_edges(i),0);
            else outline(placed,mesh,node.face);
            // A converted outline is real editable Sketch geometry.  Bind its
            // two joining vertices to the stable End vertices exactly like a
            // normally authored attached Flat.  Merely storing the placement
            // edge leaves the outline behind when corner closure shortens a
            // Bend end and can reverse the apparent segment orientation.
            bind_flat_outline_to_attachment(placed,
                imported_step_source&&!imported_corner_fallback_used?
                    0.:thickness,!imported_step_source);
        }else {
            document::prepare_bend_sketches(next.history.back(),placed,document::sheet_metal_defaults(next));
            // The imported cylinder may widen or narrow between its two
            // generatrices. Preserve that end span with the native Bend's
            // editable endpoint extensions instead of assuming equal widths.
            const auto end=sketcher::Sketch::from_serialized(next.history.back().bend.auxiliary_sketches[1]);
            const auto first=end.local_point(exit_edge->points.front());
            const auto last=end.local_point(exit_edge->points.back());
            const auto* start_reference=end.find_point(end.id+":reference:first");
            const auto* end_reference=end.find_point(end.id+":reference:last");
            const double source_start=std::min(first[0],last[0]);
            const double source_end=std::max(first[0],last[0]);
            std::array source_extensions{
                start_reference->x-source_start,
                source_end-end_reference->x};
            if(!imported_step_source)for(const auto& other:nodes) {
                if(&other==&node||!other.face.surface||
                        other.face.surface->kind!=
                            kernel::SurfaceGeometry::Kind::Cylinder)continue;
                for(const auto point:node.entry.points)
                    for(const auto other_point:other.entry.points) {
                        if(distance(point,other_point)>join_tolerance)continue;
                        const auto local=end.local_point(point);
                        const std::size_t endpoint=
                            std::abs(local[0]-source_start)<=
                                std::abs(local[0]-source_end)?0:1;
                        // Two rounded source branches meeting at this endpoint
                        // need the native zero-gap corner loft as well as the
                        // final axial setback. It tapers the end through the
                        // Bend angle instead of sweeping overlapping end
                        // sections.
                        next.history.back().bend.corner[endpoint]=true;
                        next.history.back().bend.corner_gap=0;
                    }
            }
            // First author the exact basic Bend reaching the source solid's
            // outside skin. Once all neighbouring Bends exist, their actual
            // inner endpoints determine the final correction together.
            document::set_bend_profile_extensions(next.history.back(),
                source_extensions[0],source_extensions[1]);
            document::prepare_bend_sketches(next.history.back(),placed,document::sheet_metal_defaults(next));
            const auto material=document::bend_material_definition(next.history.back(),placed,document::sheet_metal_defaults(next));
            bool supported=true;
            for(const auto& edge:mesh.edges) {
                const bool belongs=std::ranges::any_of(
                    edge.edge_treatment_side_references,
                    [&](const auto& identity) {
                        const auto fragment=edge_face_fragment(
                            mesh,identity,edge);
                        return fragment&&same_fragment(*fragment,node.face);
                    });
                if(!belongs)continue;
                for(auto point:edge.points) {
                    const auto station=coordinates(material,point);
                    const bool near_start_wrap=2*std::numbers::pi*material.neutral_radius-station.length<=join_tolerance;
                    if(std::abs(station.depth+(surface.reversed?thickness:0))>join_tolerance||station.length<-join_tolerance||
                        (station.length>material.angle*material.neutral_radius+join_tolerance&&!near_start_wrap)){supported=false;break;}
                }
                if(!supported)break;
            }
            // Do not silently replace a long or oppositely directed cylinder
            // by the shorter circular sector described by its two end normals.
            if(!supported){skip("bend span");continue;}
        }
        wave_document=std::move(next);
        wave_changed=true;
        result.created.push_back(added.id);authored[i]=added.id;
    }
        if(wave_changed) {
            PartCalculationPolicy policy;policy.reject_errors=true;
            try {
                auto calculated=calculate_part_with_resolved_references(
                    kernel,wave_document,&result.calculated,policy);
                result.document=std::move(wave_document);
                result.calculated=std::move(calculated);
            } catch(const std::exception& error) {
                if(imported_step_source&&!imported_corner_fallback_used&&
                        current_depth>1) {
                    try {
                        // Finished STEP sheet endpoints are normally exact and
                        // retain their supplier-authored spans. A multi-branch
                        // corner can nevertheless be a welded solid junction
                        // that no sequential native Add can represent without
                        // a sheet seam. Retry only after the exact wave fails:
                        // roll back its transient children, close the existing
                        // Bend corner with native endpoint identities, then
                        // rebuild that wave against the shortened End edges.
                        result.created.resize(wave_created_size);
                        result.skipped=wave_skipped;
                        for(std::size_t i=1;i<nodes.size();++i)
                            if(depth[i]==current_depth)authored[i].clear();
                        if(apply_measured_bend_corner_corrections(
                                result.document,nodes,authored,true)) {
                            result.calculated=
                                calculate_part_with_resolved_references(
                                    kernel,result.document,&result.calculated,
                                    policy);
                            imported_corner_fallback_used=true;
                            --current_depth;
                            continue;
                        }
                    } catch(const std::exception&) {}
                }
                throw std::invalid_argument("Sheet conversion wave "+
                    std::to_string(current_depth)+" failed: "+error.what());
            }
        }
    }
    if(result.skipped)throw std::invalid_argument("Sheet conversion cannot reproduce every connected face.");
    const auto edge_distance=[](const kernel::ViewerEdge& first,
            const kernel::ViewerEdge& second) {
        const auto distance=[](V a,V b) {
            const auto delta=sub(a,b);
            return std::sqrt(dot(delta,delta));
        };
        if(first.points.empty()||second.points.empty())
            return std::numeric_limits<double>::infinity();
        return std::min(
            std::max(distance(first.points.front(),second.points.front()),
                distance(first.points.back(),second.points.back())),
            std::max(distance(first.points.front(),second.points.back()),
                distance(first.points.back(),second.points.front())));
    };
    for(std::size_t continuation_index=0;
            continuation_index<continuations.size();++continuation_index) {
        const auto& continuation=continuations[continuation_index];
        const auto source_index=continuation.planes[0];
        const auto target_index=continuation.planes[1];
        if(source_index>=authored.size()||target_index>=authored.size()||
                authored[source_index].empty()||authored[target_index].empty())
            throw std::invalid_argument(
                "Sheet conversion lost a continuation branch.");
        const auto geometry=construction_reference_source_geometry(
            result.calculated);
        const auto local_geometry=source.sketch_reference_geometry_for(
            local_context,geometry);
        const auto find_edge=[&](const std::string& owner,
                const kernel::ViewerEdge& source_edge,bool bend_attachment) {
            const kernel::ViewerEdge* best_edge=nullptr;
            double best_error=INFINITY;
            for(const auto& edge:local_geometry.edges) {
                if(edge.reference.owner_id!=owner||!straight(edge))continue;
                try {
                    if(bend_attachment)
                        static_cast<void>(document::bend_sheet_references(edge));
                    else static_cast<void>(document::flat_sheet_references(edge));
                } catch(const std::invalid_argument&) {continue;}
                const double error=edge_distance(edge,source_edge);
                if(error<best_error) {
                    best_error=error;
                    best_edge=&edge;
                }
            }
            return std::pair{best_edge,best_error};
        };
        const auto [attachment,attachment_error]=find_edge(
            authored[source_index],continuation.edges[0],true);
        if(!attachment||attachment_error>join_tolerance)
            throw std::invalid_argument(
                "Sheet conversion cannot attach a continuation Bend.");
        auto next=result.document;
        auto profile=sketcher::Sketch::create_default();
        auto bend=document::PartDocument::create_sketch_container();
        bend.feature_kind=document::FeatureKind::Bend;
        bend.name="Ohyb";
        bend.bend.sketch_id=profile.id;
        bend.bend.sheet_attachment=true;
        bend.bend.thickness_override=thickness_override;
        bend.bend.thickness=thickness;
        bend.bend.radius=continuation.surface.radius-thickness;
        if(std::abs(bend.bend.radius)<=1e-7)bend.bend.radius=0;
        const auto source_normal=normal(nodes[source_index].face,
            continuation.edges[0].points.front());
        const auto target_normal=normal(nodes[target_index].face,
            continuation.edges[1].points.front());
        bend.bend.angle_degrees=std::acos(std::clamp(
            dot(source_normal,target_normal),-1.,1.))*180/
                std::numbers::pi;
        if(bend.bend.radius<0||bend.bend.angle_degrees<.001||
                bend.bend.angle_degrees>=180)
            throw std::invalid_argument(
                "Sheet conversion found invalid continuation Bend parameters.");
        profile.owner_container_id=bend.id;
        profile.name=bend.name;
        const auto attachment_delta=sub(attachment->points.back(),
            attachment->points.front());
        document::initialize_bend_start_profile(profile,
            std::sqrt(dot(attachment_delta,attachment_delta)));
        bend.placement.references=document::bend_sheet_references(*attachment);
        next.insert_history_entry(document::PartHistoryKind::Feature,bend.id);
        next.history.push_back(bend);
        next.sketches.push_back(profile);
        next.find_container(result.container_id)->
            sheet_from_body.generated_features.push_back(bend.id);
        auto transition_face=continuation.transition;
        transition_face.surface=
            std::make_shared<const kernel::SurfaceGeometry>(
                continuation.surface);
        append_source_binding(next,result.container_id,
            source_region_binding(mesh,bend.id,transition_face,
                thickness,false,"continuation-bend:"+
                    std::to_string(continuation_index),
                {{&continuation.edges[0],
                    document::SheetFromBodyEdgeRole::Entry},
                 {&continuation.edges[1],
                    document::SheetFromBodyEdgeRole::Exit}}));
        next.resolve_constructions(geometry);
        auto& placed_bend=next.sketches.back();
        document::prepare_bend_sketches(next.history.back(),placed_bend,
            document::sheet_metal_defaults(next));
        const auto preview=document::bend_preview(next.history.back(),
            placed_bend,document::sheet_metal_defaults(next));
        const auto& exit=bend_preview_edge(preview,"outer-end");
        if(edge_distance(exit,continuation.edges[1])>join_tolerance)
            throw std::invalid_argument(
                "Sheet conversion continuation Bend missed its target face.");
        // The endpoint nearest the two parent Bends is the deleted common
        // corner fillet. It identifies which end receives native zero-gap
        // corner closure; it never supplies the later face-divider direction.
        const auto parent_distance=[&](V point,std::size_t plane) {
            const auto& entry=nodes[*nodes[plane].parent].entry;
            double nearest=INFINITY;
            for(const auto candidate:entry.points) {
                const auto delta=sub(point,candidate);
                nearest=std::min(nearest,
                    std::sqrt(dot(delta,delta)));
            }
            return nearest;
        };
        const std::array meeting_scores{
            parent_distance(continuation.edges[0].points.front(),
                source_index)+
                parent_distance(continuation.edges[1].points.front(),
                    target_index),
            parent_distance(continuation.edges[0].points.back(),
                source_index)+
                parent_distance(continuation.edges[1].points.back(),
                    target_index)};
        const auto& first_end=bend_preview_edge(preview,"outer-first");
        const auto point_distance=[](V first,V second) {
            const auto delta=sub(first,second);
            return std::sqrt(dot(delta,delta));
        };
        const std::size_t preview_first_source=
            point_distance(first_end.points.front(),
                attachment->points.front())<=
            point_distance(first_end.points.front(),
                attachment->points.back())?0:1;
        const std::size_t source_meeting=
            meeting_scores[1]<meeting_scores[0]?1:0;
        next.history.back().bend.corner[
            preview_first_source==source_meeting?0:1]=true;
        next.history.back().bend.corner_gap=0;
        document::prepare_bend_sketches(next.history.back(),placed_bend,
            document::sheet_metal_defaults(next));
        PartCalculationPolicy policy;
        policy.reject_errors=true;
        auto calculated=calculate_part_with_resolved_references(
            kernel,next,&result.calculated,policy);
        result.document=std::move(next);
        result.calculated=std::move(calculated);
        result.created.push_back(bend.id);

        const auto flat_geometry=construction_reference_source_geometry(
            result.calculated);
        const auto local_flat_geometry=source.sketch_reference_geometry_for(
            local_context,flat_geometry);
        const kernel::ViewerEdge* flat_attachment=nullptr;
        double flat_attachment_error=INFINITY;
        for(const auto& edge:local_flat_geometry.edges) {
            if(edge.reference.owner_id!=bend.id||!straight(edge))continue;
            try {
                static_cast<void>(document::flat_sheet_references(edge));
            } catch(const std::invalid_argument&) {continue;}
            const double error=edge_distance(edge,continuation.edges[1]);
            if(error<flat_attachment_error) {
                flat_attachment_error=error;
                flat_attachment=&edge;
            }
        }
        if(!flat_attachment||flat_attachment_error>join_tolerance)
            throw std::invalid_argument(
                "Sheet conversion cannot attach a continuation Flat.");
        auto flat_next=result.document;
        auto flat_profile=sketcher::Sketch::create_default();
        auto flat=document::PartDocument::create_sketch_container();
        flat.feature_kind=document::FeatureKind::Flat;
        flat.name="Tabule";
        flat.flat.sketch_id=flat_profile.id;
        flat.flat.sheet_attachment=true;
        flat.flat.thickness_override=thickness_override;
        flat.flat.thickness=thickness;
        flat.flat.direction=document::ExtrusionDirection::Reverse;
        flat.placement.references=
            document::flat_sheet_references(*flat_attachment);
        flat_profile.owner_container_id=flat.id;
        flat_profile.name=flat.name;
        flat_next.insert_history_entry(document::PartHistoryKind::Feature,
            flat.id);
        flat_next.history.push_back(flat);
        flat_next.sketches.push_back(flat_profile);
        flat_next.find_container(result.container_id)->
            sheet_from_body.generated_features.push_back(flat.id);
        append_source_binding(flat_next,result.container_id,
            source_region_binding(mesh,flat.id,nodes[target_index].face,
                thickness,false,"continuation-flat:"+
                    std::to_string(continuation_index),
                {{&continuation.edges[1],
                    document::SheetFromBodyEdgeRole::Entry}}));
        flat_next.resolve_constructions(flat_geometry);
        auto& placed_flat=flat_next.sketches.back();
        const auto& target_surface=*nodes[target_index].face.surface;
        if(std::abs(dot(placed_flat.resolved_normal,target_surface.axis))<
                1-1e-6)
            throw std::invalid_argument(
                "Sheet conversion continuation Flat has the wrong frame.");
        const auto target_incoming=incoming_edges(target_index);
        const auto& target_group=continuations_by_target.at(target_index);
        const auto position=std::ranges::find(
            target_group,continuation_index);
        if(position==target_group.end())throw std::invalid_argument(
            "Sheet conversion lost a continuation region.");
        const std::size_t region=1+static_cast<std::size_t>(
            position-target_group.begin());
        split_outline(placed_flat,mesh,nodes[target_index].face,
            target_incoming,region);
        bind_flat_outline_to_attachment(placed_flat,thickness,
            !imported_step_source);
        auto flat_calculated=calculate_part_with_resolved_references(
            kernel,flat_next,&result.calculated,policy);
        result.document=std::move(flat_next);
        result.calculated=std::move(flat_calculated);
        result.created.push_back(flat.id);
    }
    if(!imported_step_source&&apply_measured_bend_corner_corrections(
            result.document,nodes,authored)) {
        PartCalculationPolicy policy;policy.reject_errors=true;
        result.calculated=calculate_part_with_resolved_references(
            kernel,result.document,&result.calculated,policy);
    }
    if(!imported_step_source&&apply_remaining_bend_corner_corrections(
            result.document,result.created)) {
        PartCalculationPolicy policy;policy.reject_errors=true;
        result.calculated=calculate_part_with_resolved_references(
            kernel,result.document,&result.calculated,policy);
    }
    result.document.validate_sheet_from_body();
    return result;
}

namespace {
bool source_reference_matches(const kernel::FaceReference& actual,
        const kernel::FaceReference& expected) {
    return actual.owner_id==expected.owner_id&&
        actual.semantic_key==expected.semantic_key&&
        actual.instance_path==expected.instance_path;
}

bool source_reference_matches(const kernel::EdgeReference& actual,
        const kernel::EdgeReference& expected) {
    if(actual.owner_id!=expected.owner_id||
            actual.instance_path!=expected.instance_path)return false;
    if(actual.semantic_key==expected.semantic_key)return true;
    // A union boundary edge keeps its deterministic parent but its compact
    // adjacency digest can differ between an isolated source-prefix
    // calculation and the same prefix inside the complete document. For this
    // association detector only, compare that stable parent portion. General
    // persisted modeling references retain their exact identity contract.
    const auto stable_parent=[](std::string_view key) {
        const auto digest=key.find(":context-digest:");
        return key.substr(0,digest);
    };
    return actual.semantic_key.find(":context-digest:")!=std::string::npos&&
        expected.semantic_key.find(":context-digest:")!=std::string::npos&&
        stable_parent(actual.semantic_key)==stable_parent(
            expected.semantic_key);
}

bool close_value(double first,double second,double tolerance=1e-7) {
    return std::abs(first-second)<=tolerance;
}

bool close_vector(V first,V second,double tolerance=1e-7) {
    return close_value(first.x,second.x,tolerance)&&
        close_value(first.y,second.y,tolerance)&&
        close_value(first.z,second.z,tolerance);
}

bool same_surface_signature(
        const document::SheetFromBodySurfaceSignature& first,
        const document::SheetFromBodySurfaceSignature& second) {
    if(first.kind!=second.kind||first.reversed!=second.reversed)
        return false;
    const auto first_axis=unit(first.axis);
    const auto second_axis=unit(second.axis);
    const double alignment=dot(first_axis,second_axis);
    if(std::abs(alignment)<1-1e-8)return false;
    if(first.kind==kernel::SurfaceGeometry::Kind::Plane)
        return std::abs(dot(sub(second.origin,first.origin),first_axis))<=
            1e-7;
    if(!close_value(first.radius,second.radius))return false;
    const auto line_offset=cross(sub(second.origin,first.origin),first_axis);
    if(std::sqrt(dot(line_offset,line_offset))>1e-7)return false;
    // Axial trimming belongs to the stable boundary edges. A compact Body
    // boundary may expose the same cylinder support with a shorter current
    // fragment than its exact authored reference; comparing those ranges
    // would turn ordinary cache compaction into a false source edit.
    return true;
}

bool same_edge_endpoints(const std::array<V,2>& first,
        const kernel::ViewerEdge& second) {
    if(second.points.size()<2)return false;
    const std::array<V,2> endpoints{
        second.points.front(),second.points.back()};
    return (close_vector(first[0],endpoints[0])&&
            close_vector(first[1],endpoints[1]))||
        (close_vector(first[0],endpoints[1])&&
            close_vector(first[1],endpoints[0]));
}

kernel::ViewerMesh source_mesh_in_target_coordinates(
        const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& calculated,
        const document::HistoryContainer& conversion) {
    if(calculated.empty())return {};
    const auto* target=source.body_owner_for_object(conversion.id);
    const auto* source_body=source.body_history.find(
        conversion.sheet_from_body.source_body_id);
    if(!target||!source_body)return {};
    kernel::ViewerMesh mesh;
    if(source_body==target) {
        std::size_t operation_count{};
        for(const auto& entry:target->entries) {
            if(entry.id==conversion.id)break;
            if(entry.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=source.find_container(entry.id);
            if(feature&&feature->feature_kind!=document::FeatureKind::Sketch&&
                    feature->feature_kind!=
                        document::FeatureKind::SheetProfile)
                ++operation_count;
        }
        const auto boundaries=calculated.back().body_boundaries.find(
            source_body->scope.id);
        if(!operation_count||boundaries==calculated.back().
                body_boundaries.end()||
                operation_count>boundaries->second.size())return {};
        // Body histories are local. Move this exact pre-conversion boundary
        // through world space before expressing it in the target frame, just
        // like the original conversion path does for a final Body output.
        mesh=source.place_body_mesh(
            boundaries->second[operation_count-1].mesh,
            source_body->scope.id);
    } else {
        const auto output=calculated.back().body_outputs.find(
            source_body->scope.id);
        if(output==calculated.back().body_outputs.end())return {};
        mesh=output->second->mesh;
    }
    // Intermediate Body boundaries deliberately compact their authored
    // reference database. The final aggregate retains the exact source
    // supports in world coordinates; use those supports for association
    // comparison while keeping the pre-conversion current edges above.
    // Keep the exact pre-conversion references: Boolean split edges that
    // define a converted region may no longer be published by the final
    // aggregate after the generated sheet replaces the source Body. Add the
    // aggregate database only as a supplement for authored supports that an
    // intermediate boundary compacted away.
    document::history_reference_detail::append(mesh.original_references,
        calculated.back().mesh.original_references);
    auto local=sketcher::Sketch::create_default();
    local.owner_container_id=target->scope.id;
    kernel::ViewerReferenceGeometry packet{std::move(mesh.vertices),
        std::move(mesh.triangles),std::move(mesh.triangle_references),
        std::move(mesh.edges),std::move(mesh.points),std::move(mesh.axes)};
    packet=source.sketch_reference_geometry_for(local,std::move(packet));
    mesh.vertices=std::move(packet.vertices);
    mesh.triangles=std::move(packet.triangles);
    mesh.triangle_references=std::move(packet.triangle_references);
    mesh.edges=std::move(packet.edges);
    mesh.points=std::move(packet.points);
    mesh.axes=std::move(packet.axes);
    mesh.original_references=source.sketch_reference_geometry_for(
        local,std::move(mesh.original_references));
    return mesh;
}

const Face* find_source_face(const kernel::ViewerMesh& mesh,
        const kernel::FaceReference& reference,
        const document::SheetFromBodySurfaceSignature* previous=nullptr) {
    const Face* result=nullptr;
    double best=std::numeric_limits<double>::infinity();
    const auto consider=[&](const auto& candidates) {
        for(const auto& candidate:candidates) {
            if(!source_reference_matches(candidate,reference)||
                    !candidate.surface)continue;
            double score{};
            if(previous) {
                const auto current=surface_signature(candidate);
                if(current.kind!=previous->kind)score+=1e12;
                else {
                    const auto a=unit(previous->axis);
                    const auto b=unit(current.axis);
                    score+=1e6*(1-std::abs(dot(a,b)));
                    if(current.kind==kernel::SurfaceGeometry::Kind::Plane)
                        score+=std::abs(dot(sub(current.origin,
                            previous->origin),a));
                    else {
                        score+=std::abs(current.radius-previous->radius);
                        const auto offset=cross(sub(current.origin,
                            previous->origin),a);
                        score+=std::sqrt(dot(offset,offset));
                    }
                    if(current.reversed!=previous->reversed)score+=1e9;
                }
            }
            if(score<best) {best=score;result=&candidate;}
        }
    };
    consider(mesh.original_references.triangle_references);
    consider(mesh.triangle_references);
    return result;
}

const kernel::ViewerEdge* find_source_edge(const kernel::ViewerMesh& mesh,
        const kernel::EdgeReference& reference,
        const std::array<V,2>* previous=nullptr) {
    const kernel::ViewerEdge* result=nullptr;
    double best=std::numeric_limits<double>::infinity();
    const auto distance=[](V first,V second) {
        const auto delta=sub(first,second);
        return std::sqrt(dot(delta,delta));
    };
    const auto consider=[&](const auto& candidates) {
        for(const auto& candidate:candidates) {
            if(!source_reference_matches(candidate.reference,reference)||
                    candidate.points.size()<2)continue;
            double score{};
            if(previous)score=std::min(
                std::max(distance((*previous)[0],candidate.points.front()),
                    distance((*previous)[1],candidate.points.back())),
                std::max(distance((*previous)[0],candidate.points.back()),
                    distance((*previous)[1],candidate.points.front())));
            if(score<best) {best=score;result=&candidate;}
        }
    };
    consider(mesh.original_references.edges);
    consider(mesh.edges);
    return result;
}

const kernel::ViewerEdge* find_source_edge_by_geometry(
        const kernel::ViewerMesh& mesh,
        const document::SheetFromBodyEdgeBinding& binding) {
    const kernel::ViewerEdge* result=nullptr;
    int best=-1;
    const auto consider=[&](const auto& candidates) {
        for(const auto& candidate:candidates) {
            if(!candidate.reference.valid()||
                    !same_edge_endpoints(binding.last_valid_endpoints,
                        candidate))continue;
            int score{};
            if(candidate.reference.owner_id==binding.reference.owner_id)
                score+=2;
            if(source_reference_matches(candidate.reference,
                    binding.reference))score+=4;
            if(score>best) {best=score;result=&candidate;}
        }
    };
    consider(mesh.original_references.edges);
    consider(mesh.edges);
    return result;
}

void replace_generated_identity(std::string& value,
        const std::vector<std::pair<std::string,std::string>>& replacements) {
    for(const auto& [before,after]:replacements) {
        if(before.empty()||before==after)continue;
        std::size_t position{};
        while((position=value.find(before,position))!=std::string::npos) {
            value.replace(position,before.size(),after);
            position+=after.size();
        }
    }
}

void replace_generated_reference(document::ConstructionReference& reference,
        const std::vector<std::pair<std::string,std::string>>& replacements) {
    replace_generated_identity(reference.owner_id,replacements);
    replace_generated_identity(reference.semantic_key,replacements);
    replace_generated_identity(reference.instance_path,replacements);
    for(auto& source:reference.branch_sources)
        for(auto& field:source)
            replace_generated_identity(field,replacements);
}

const sketcher::Sketch* owned_sketch(
        const document::PartDocument& source,const std::string& owner) {
    const auto found=std::ranges::find(source.sketches,owner,
        &sketcher::Sketch::owner_container_id);
    return found==source.sketches.end()?nullptr:&*found;
}

sketcher::Sketch* owned_sketch(
        document::PartDocument& source,const std::string& owner) {
    const auto found=std::ranges::find(source.sketches,owner,
        &sketcher::Sketch::owner_container_id);
    return found==source.sketches.end()?nullptr:&*found;
}
}

bool refresh_sheet_from_body_associations(
        document::PartDocument& source,
        const std::vector<kernel::BodyResult>& calculated,
        const kernel::OcctKernel& kernel) {
    if(calculated.empty()||sheet_conversion_authoring_depth)return false;
    bool document_changed{};
    // Work from stable parent IDs because a successful refresh replaces the
    // parent's binding vectors and invalidates references into `history`.
    std::vector<std::string> conversions;
    for(const auto& feature:source.history)
        if(feature.feature_kind==document::FeatureKind::SheetFromBody&&
                !feature.sheet_from_body.child_bindings.empty())
            conversions.push_back(feature.id);
    for(const auto& conversion_id:conversions) {
        auto* conversion=source.find_container(conversion_id);
        if(!conversion)continue;
        std::vector<kernel::BodyResult> ripped_source;
        if(!conversion->sheet_from_body.rip_sketches.empty()) {
            std::vector<const sketcher::Sketch*> rip_sources;
            for(const auto& id:conversion->sheet_from_body.rip_sketches) {
                const auto sketch=std::ranges::find(source.sketches,id,
                    &sketcher::Sketch::id);
                if(sketch==source.sketches.end()||sketch->suppressed) {
                    rip_sources.clear();
                    break;
                }
                rip_sources.push_back(&*sketch);
            }
            if(rip_sources.empty()) {
                for(auto& binding:conversion->sheet_from_body.child_bindings)
                    if(!binding.detached&&binding.references_valid) {
                        binding.references_valid=false;
                        document_changed=true;
                    }
                continue;
            }
            try {
                ripped_source=calculate_ripped_source(source,calculated,
                    conversion->sheet_from_body.source_body_id,rip_sources,
                    kernel);
            } catch(const std::exception&) {
                for(auto& binding:conversion->sheet_from_body.child_bindings)
                    if(!binding.detached&&binding.references_valid) {
                        binding.references_valid=false;
                        document_changed=true;
                    }
                continue;
            }
        }
        auto source_mesh=source_mesh_in_target_coordinates(source,
            ripped_source.empty()?calculated:ripped_source,*conversion);
        bool missing{};
        bool changed_source{};
        for(auto& binding:conversion->sheet_from_body.child_bindings) {
            if(binding.detached)continue;
            bool binding_missing{};
            bool binding_changed=!binding.references_valid;
            for(std::size_t face_index=0;face_index<binding.faces.size();
                    ++face_index) {
                const auto& face=binding.faces[face_index];
                const auto* current=find_source_face(source_mesh,
                    face.reference,&face.last_valid_surface);
                if(!current) {
                    // The primary source support owns the stable region. Its
                    // loss is broken topology. Opposite/adjacent supports may
                    // legitimately receive new Boolean edge identities after
                    // a source parameter edit; let reconstruction replace
                    // that complete binding instead of treating it as lost.
                    if(face_index==0)binding_missing=true;
                    else binding_changed=true;
                    continue;
                }
                const bool differs=!same_surface_signature(
                    face.last_valid_surface,surface_signature(*current));
                binding_changed|=differs;
            }
            for(const auto& edge:binding.edges) {
                const auto* current=find_source_edge(source_mesh,
                    edge.reference,&edge.last_valid_endpoints);
                if(!current) {
                    binding_changed=true;
                    continue;
                }
                const bool differs=!same_edge_endpoints(
                    edge.last_valid_endpoints,*current);
                binding_changed|=differs;
            }
            if(binding_missing) {
                missing=true;
                if(binding.references_valid) {
                    binding.references_valid=false;
                    document_changed=true;
                }
            } else changed_source|=binding_changed;
        }
        // Broken topology retains every last-valid Flat/Bend. A later source
        // repair is detected by the same stable references and can refresh it.
        if(missing||!changed_source)continue;

        const auto old_definition=conversion->sheet_from_body;
        const auto* target=source.body_owner_for_object(conversion_id);
        if(!target)continue;
        const auto target_id=target->scope.id;
        auto prefix=source;
        const auto* prefix_target=prefix.body_history.find(target_id);
        const auto boundary=std::ranges::find(prefix_target->entries,
            conversion_id,&document::PartHistoryEntry::id);
        if(boundary==prefix_target->entries.end())continue;
        std::vector<std::string> trailing;
        for(auto entry=boundary;entry!=prefix_target->entries.end();++entry)
            trailing.push_back(entry->id);
        for(auto entry=trailing.rbegin();entry!=trailing.rend();++entry)
            if(prefix.find_container(*entry)||
                    prefix.find_construction(*entry))
                prefix.erase_history_object(*entry);
        prefix.body_history.activate(target_id);
        prefix.body_history.set_history_cursor(target_id,
            prefix.body_history.find(target_id)->entries.size());

        PartCalculationPolicy source_policy;
        source_policy.reject_errors=true;
        SheetBodyConversion rebuilt;
        try {
            auto source_cache=calculate_part(kernel,prefix,nullptr,
                source_policy);
            if(source_cache.empty())throw std::runtime_error(
                "Sheet from Body source prefix has no calculated output");
            const auto source_output=source_cache.back().body_outputs.find(
                old_definition.source_body_id);
            if(source_output==source_cache.back().body_outputs.end())
                throw std::runtime_error(
                    "Sheet from Body source prefix is unavailable");
            if(!old_definition.source_face.reference.valid())
                throw std::runtime_error(
                    "Sheet from Body root binding is unavailable");
            const auto* selected=find_source_face(source_output->second->mesh,
                old_definition.source_face.reference,
                &old_definition.source_face.last_valid_surface);
            if(!selected)throw std::runtime_error(
                "Sheet from Body root source face is unavailable");
            std::vector<Face> included;
            for(const auto& binding:old_definition.included_faces) {
                const auto* face=find_source_face(source_output->second->mesh,
                    binding.reference,&binding.last_valid_surface);
                if(!face)throw std::runtime_error(
                    "An included Sheet from Body source face is unavailable");
                included.push_back(*face);
            }
            rebuilt=prepare_sheet_from_body(prefix,source_cache,*selected,
                old_definition.thickness,kernel,
                old_definition.thickness_override,std::move(included),
                old_definition.rip_sketches);
        } catch(const std::exception&) {
            // A changed source that can no longer be represented is a broken
            // association, not permission to discard the last valid sheet.
            conversion=source.find_container(conversion_id);
            for(auto& binding:conversion->sheet_from_body.child_bindings)
                if(!binding.detached&&binding.references_valid) {
                    binding.references_valid=false;
                    document_changed=true;
                }
            continue;
        }

        const auto* rebuilt_parent=rebuilt.document.find_container(
            rebuilt.container_id);
        if(!rebuilt_parent)continue;
        std::map<std::string,const document::SheetFromBodyChildBinding*>
            old_regions;
        for(const auto& binding:old_definition.child_bindings)
            old_regions.emplace(binding.region_id,&binding);
        std::map<std::string,const document::SheetFromBodyChildBinding*>
            new_regions;
        for(const auto& binding:rebuilt_parent->sheet_from_body.child_bindings)
            new_regions.emplace(binding.region_id,&binding);
        bool compatible=old_regions.size()==new_regions.size();
        for(const auto& [region,binding]:old_regions)
            compatible=compatible&&new_regions.contains(region)&&
                binding->bend==new_regions.at(region)->bend;
        std::vector<std::string> mapped_order;
        if(compatible)for(const auto& feature_id:
                rebuilt_parent->sheet_from_body.generated_features) {
            const auto binding=std::ranges::find(
                rebuilt_parent->sheet_from_body.child_bindings,feature_id,
                &document::SheetFromBodyChildBinding::feature_id);
            if(binding==rebuilt_parent->sheet_from_body.child_bindings.end()||
                    !old_regions.contains(binding->region_id)) {
                compatible=false;break;
            }
            mapped_order.push_back(
                old_regions.at(binding->region_id)->feature_id);
        }
        compatible=compatible&&mapped_order==old_definition.generated_features;
        if(!compatible) {
            conversion=source.find_container(conversion_id);
            for(auto& binding:conversion->sheet_from_body.child_bindings)
                if(!binding.detached&&binding.references_valid) {
                    binding.references_valid=false;
                    document_changed=true;
                }
            continue;
        }

        std::vector<std::pair<std::string,std::string>> replacements;
        replacements.emplace_back(rebuilt.container_id,conversion_id);
        for(const auto& [region,new_binding]:new_regions) {
            const auto* old_binding=old_regions.at(region);
            replacements.emplace_back(new_binding->feature_id,
                old_binding->feature_id);
            const auto* new_feature=rebuilt.document.find_container(
                new_binding->feature_id);
            const auto* old_feature=source.find_container(
                old_binding->feature_id);
            if(new_feature&&old_feature) {
                replacements.emplace_back(new_feature->feature_id,
                    old_feature->feature_id);
                const auto* new_sketch=owned_sketch(rebuilt.document,
                    new_feature->id);
                const auto* old_sketch=owned_sketch(source,old_feature->id);
                if(new_sketch&&old_sketch)
                    replacements.emplace_back(new_sketch->id,old_sketch->id);
            }
        }

        for(const auto& [region,new_binding]:new_regions) {
            const auto* old_binding=old_regions.at(region);
            if(old_binding->detached)continue;
            const auto* generated=rebuilt.document.find_container(
                new_binding->feature_id);
            auto* current=source.find_container(old_binding->feature_id);
            const auto* generated_sketch=generated?owned_sketch(
                rebuilt.document,generated->id):nullptr;
            auto* current_sketch=current?
                owned_sketch(source,current->id):nullptr;
            if(!generated||!current||!generated_sketch||!current_sketch)
                continue;
            auto next=*current;
            next.placement=generated->placement;
            for(auto& reference:next.placement.references)
                replace_generated_reference(reference,replacements);
            if(next.feature_kind==document::FeatureKind::Flat) {
                next.flat=generated->flat;
                replace_generated_identity(next.flat.sketch_id,replacements);
            } else {
                next.bend=generated->bend;
                replace_generated_identity(next.bend.sketch_id,replacements);
                for(auto& sketch:next.bend.auxiliary_sketches)
                    replace_generated_identity(sketch,replacements);
            }
            std::string sketch_data=generated_sketch->serialized();
            replace_generated_identity(sketch_data,replacements);
            auto next_sketch=sketcher::Sketch::from_serialized(sketch_data);
            if(next!=*current) {
                *current=std::move(next);
                document_changed=true;
            }
            if(next_sketch.serialized()!=current_sketch->serialized()) {
                *current_sketch=std::move(next_sketch);
                document_changed=true;
            }
        }

        conversion=source.find_container(conversion_id);
        auto next_definition=conversion->sheet_from_body;
        next_definition.generated_features=std::move(mapped_order);
        next_definition.child_bindings.clear();
        for(const auto& new_binding:
                rebuilt_parent->sheet_from_body.child_bindings) {
            auto next=new_binding;
            const auto* old_binding=old_regions.at(next.region_id);
            next.feature_id=old_binding->feature_id;
            next.detached=old_binding->detached;
            if(next.detached)next=*old_binding;
            else {
                next.references_valid=true;
                // The isolated source-prefix calculation may publish a
                // Boolean boundary under an equivalent, but context-specific,
                // reference. Persist the identity visible in the complete
                // document so the next settling pass observes the regenerated
                // result as stable. Edges that the complete source boundary
                // cannot publish are not usable association inputs.
                for(auto& edge:next.edges)
                    if(const auto* current=find_source_edge(source_mesh,
                            edge.reference,&edge.last_valid_endpoints))
                        edge.reference=persistent_edge_reference(
                            current->reference);
                    else if(const auto* current=
                            find_source_edge_by_geometry(source_mesh,edge))
                        edge.reference=persistent_edge_reference(
                            current->reference);
                std::erase_if(next.edges,[&](const auto& edge) {
                    return !find_source_edge(source_mesh,edge.reference,
                        &edge.last_valid_endpoints);
                });
            }
            next_definition.child_bindings.push_back(std::move(next));
        }
        if(next_definition!=conversion->sheet_from_body) {
            conversion->sheet_from_body=std::move(next_definition);
            document_changed=true;
        }
        source.validate_sheet_from_body();
    }
    return document_changed;
}

bool explode_sheet_from_body(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,const std::string& conversion_id,
        std::optional<std::string> child_id) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument(
        "Sheet from Body is available only in a Part.");
    const auto& before=state->session.document();
    const auto* conversion=before.find_container(conversion_id);
    if(!conversion||conversion->feature_kind!=
            document::FeatureKind::SheetFromBody)
        throw std::invalid_argument("Sheet from Body does not exist.");
    const auto* owner=before.body_owner_for_object(conversion_id);
    if(!owner||owner->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument(
            "Activate the Body owning this Sheet from Body.");

    auto next=before;
    auto* next_conversion=next.find_container(conversion_id);
    if(child_id) {
        const auto binding=std::ranges::find(
            next_conversion->sheet_from_body.child_bindings,*child_id,
            &document::SheetFromBodyChildBinding::feature_id);
        if(binding==next_conversion->sheet_from_body.child_bindings.end())
            throw std::invalid_argument(
                "The selected feature is not a bound Sheet from Body part.");
        if(binding->detached)return false;
        binding->detached=true;
        // Detaching changes only the association owner and Tree nesting. The
        // native Flat/Bend definition and its calculated material are byte-for-
        // byte unchanged, so publishing the current cache avoids an unrelated
        // OCCT replay and still creates exactly one Undo transaction.
        commit_part_document(live,id,std::move(next),
            state->session.calculated_boundaries());
        return true;
    } else {
        auto graph=next.body_history;
        auto body=*graph.find(owner->scope.id);
        const auto row=std::ranges::find(body.entries,conversion_id,
            &document::PartHistoryEntry::id);
        if(row==body.entries.end())
            throw std::invalid_argument(
                "Sheet from Body is missing from Body history");
        const auto index=static_cast<std::size_t>(
            row-body.entries.begin());

        if(next_conversion->sheet_from_body.replaces_body) {
            // The conversion marker is an empty replacement operation: it
            // removes the same-Body source solid before the generated sheet
            // starts. Exploding the marker must preserve that exact material
            // boundary. Retain the authored source prefix for inspection and
            // Undo, but suppress its material-producing features.
            for(auto entry=body.entries.begin();entry!=row;++entry)
                if(entry->kind==document::PartHistoryKind::Feature)
                    if(auto* source=next.find_container(entry->id))
                        source->suppressed=true;
        }
        // A complete explode removes the association owner, so every retained
        // native child must also be able to regenerate without its former
        // attachment lookup. Freeze the already resolved authored frame into
        // the ordinary numeric placement. Sequential Sketch endpoint contacts
        // remain valid native child-to-child dependencies, while no Flat/Bend
        // asks the removed conversion boundary to resolve its placement again.
        for(const auto& generated:
                next_conversion->sheet_from_body.generated_features) {
            auto* child=next.find_container(generated);
            auto* sketch=child?owned_sketch(next,child->id):nullptr;
            if(!child||!sketch)continue;
            const auto origin=sketch->resolved_origin;
            const auto x=sketch->resolved_x_axis;
            const auto y=sketch->resolved_y_axis;
            const auto normal=sketch->resolved_normal;
            child->placement.references.clear();
            child->placement.reference_valid=true;
            child->placement.orientation_back=false;
            child->placement.orientation_quarter_turns=0;
            child->placement.rotation_offset_x=0;
            child->placement.rotation_offset_y=0;
            child->placement.rotation_offset_z=0;
            numeric_frame(*child,*sketch,origin,x,y,normal);
            if(child->feature_kind==document::FeatureKind::Flat)
                child->flat.sheet_attachment=false;
            else if(child->feature_kind==document::FeatureKind::Bend)
                child->bend.sheet_attachment=false;
        }
        std::erase_if(next.history,[&](const auto& feature) {
            return feature.id==conversion_id;
        });
        std::erase_if(next.sketches,[&](const auto& sketch) {
            return sketch.owner_container_id==conversion_id;
        });
        body.entries.erase(row);
        if(index<body.cursor)--body.cursor;
        graph.update_body(std::move(body));
        next.set_body_history(std::move(graph));
    }

    PartCalculationPolicy policy;
    policy.reject_errors=true;
    // Removing the replacement marker changes native operation indices. A
    // previous incremental boundary can consequently identify the right
    // generated feature at the wrong input boundary. Rebuild this explicit
    // one-time explode operation cold, then settle the unchanged attachment
    // references against those correctly indexed boundaries.
    auto calculated=calculate_part(kernel,next,nullptr,policy);
    calculated=calculate_part_with_resolved_references(kernel,next,
        &calculated,policy);
    commit_part_document(live,id,std::move(next),std::move(calculated));
    return true;
}
}
