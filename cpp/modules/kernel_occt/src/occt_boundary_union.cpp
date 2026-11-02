#include <zima/kernel/occt_boundary_union.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <NCollection_List.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace zima::kernel {
namespace {

using Clock=std::chrono::steady_clock;

template <typename Reference>
std::string reference_key(const Reference& reference) {
    return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+
        reference.instance_path;
}

template <typename Reference>
std::string encoded_reference(const Reference& reference) {
    return std::to_string(reference.owner_id.size())+":"+
        reference.owner_id+std::to_string(reference.semantic_key.size())+":"+
        reference.semantic_key+std::to_string(reference.instance_path.size())+
        ":"+reference.instance_path;
}

std::string encoded_reference_set(const std::set<std::string>& references) {
    std::string encoded;
    for(const auto& reference:references)
        encoded+=std::to_string(reference.size())+":"+reference;
    return encoded;
}

std::string compact_reference_context(std::string_view value) {
    std::uint64_t first=14695981039346656037ULL;
    std::uint64_t second=7809847782465536322ULL;
    for(const unsigned char byte:value) {
        first^=byte;
        first*=1099511628211ULL;
        second^=static_cast<std::uint64_t>(byte)+0x9eU;
        second*=14029467366897019727ULL;
    }
    std::ostringstream encoded;
    encoded<<std::hex<<first<<':'<<second;
    return encoded.str();
}

std::string direct_boundary_key(std::string_view name_space,
        std::string_view role,
        const std::string& primary,
        const std::set<std::string>& context) {
    return std::string(name_space)+std::string(role)+":parent:"+
        std::to_string(primary.size())+":"+primary+":context-digest:"+
        compact_reference_context(encoded_reference_set(context));
}

bool edge_is_covered_by_curve(const TopoDS_Edge& edge,
        const OcctBoundaryCurve& parent,double tolerance) {
    double first{},last{};
    const auto curve=BRep_Tool::Curve(edge,first,last);
    if(curve.IsNull()||parent.curve.IsNull()||last<=first||
       parent.last_parameter<=parent.first_parameter)return false;
    GeomAPI_ProjectPointOnCurve projection;
    projection.Init(parent.curve,parent.first_parameter,
        parent.last_parameter);
    constexpr int samples=7;
    for(int sample=0;sample<samples;++sample) {
        gp_Pnt point;
        curve->D0(first+(last-first)*sample/(samples-1.),point);
        projection.Perform(point);
        if(!projection.NbPoints()||projection.LowerDistance()>
                std::max(1.0e-6,2.*tolerance))return false;
    }
    return true;
}

bool edge_is_covered_by_edge(const TopoDS_Edge& child,
        const TopoDS_Edge& parent,double tolerance) {
    if(child.IsSame(parent))return true;
    double child_first{},child_last{},parent_first{},parent_last{};
    const auto child_curve=BRep_Tool::Curve(
        child,child_first,child_last);
    const auto parent_curve=BRep_Tool::Curve(
        parent,parent_first,parent_last);
    if(child_curve.IsNull()||parent_curve.IsNull()||
       child_last<=child_first||parent_last<=parent_first)
        return false;
    GeomAPI_ProjectPointOnCurve projection;
    projection.Init(parent_curve,parent_first,parent_last);
    constexpr int samples=9;
    for(int sample=0;sample<samples;++sample) {
        gp_Pnt point;
        child_curve->D0(child_first+(child_last-child_first)*sample/
            (samples-1.),point);
        projection.Perform(point);
        if(!projection.NbPoints()||projection.LowerDistance()>
                std::max(1.0e-6,2.*tolerance))
            return false;
    }
    return true;
}

double face_area(const TopoDS_Face& face) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face,properties);
    return std::abs(properties.Mass());
}

std::optional<std::pair<double,double>> interior_parameters(
        const TopoDS_Face& face,double tolerance) {
    double u_min{},u_max{},v_min{},v_max{};
    BRepTools::UVBounds(face,u_min,u_max,v_min,v_max);
    for(int grid=1;grid<=13;++grid)
        for(int u=0;u<grid;++u)
            for(int v=0;v<grid;++v) {
                const double parameter_u=u_min+(u+.5)/grid*(u_max-u_min);
                const double parameter_v=v_min+(v+.5)/grid*(v_max-v_min);
                BRepClass_FaceClassifier classifier(face,
                    gp_Pnt2d(parameter_u,parameter_v),tolerance,true);
                if(classifier.State()==TopAbs_IN)
                    return std::pair{parameter_u,parameter_v};
            }
    return std::nullopt;
}

bool curve_is_face_boundary(const OcctBoundaryCurve& curve,
        const TopoDS_Face& face,double tolerance) {
    struct Boundary {
        occ::handle<Geom_Curve> curve;
        double first{};
        double last{};
    };
    std::vector<Boundary> boundaries;
    for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
        double first{},last{};
        auto geometry=BRep_Tool::Curve(
            TopoDS::Edge(edge.Current()),first,last);
        if(!geometry.IsNull()&&last>first)
            boundaries.push_back({std::move(geometry),first,last});
    }
    constexpr int samples=9;
    for(int sample=0;sample<samples;++sample) {
        gp_Pnt point;
        curve.curve->D0(curve.first_parameter+
            (curve.last_parameter-curve.first_parameter)*sample/
                (samples-1.),point);
        bool covered=false;
        for(const auto& boundary:boundaries) {
            GeomAPI_ProjectPointOnCurve projection(point,boundary.curve,
                boundary.first,boundary.last);
            if(projection.NbPoints()&&projection.LowerDistance()<=
                    std::max(1.0e-6,2.*tolerance)) {
                covered=true;
                break;
            }
        }
        if(!covered)return false;
    }
    return true;
}

bool edge_is_covered_by_face(const TopoDS_Edge& edge,
        const TopoDS_Face& face,double tolerance) {
    double first{},last{};
    const auto curve=BRep_Tool::Curve(edge,first,last);
    if(curve.IsNull()||last<=first)return false;
    std::vector<std::tuple<occ::handle<Geom_Curve>,double,double>> candidates;
    for(TopExp_Explorer candidate(face,TopAbs_EDGE);candidate.More();
            candidate.Next()) {
        double begin{},end{};
        auto geometry=BRep_Tool::Curve(
            TopoDS::Edge(candidate.Current()),begin,end);
        if(!geometry.IsNull()&&end>begin)
            candidates.emplace_back(std::move(geometry),begin,end);
    }
    constexpr int samples=9;
    for(int sample=0;sample<samples;++sample) {
        gp_Pnt point;
        curve->D0(first+(last-first)*sample/(samples-1.),point);
        bool covered=false;
        for(const auto& [candidate,begin,end]:candidates) {
            GeomAPI_ProjectPointOnCurve projection(
                point,candidate,begin,end);
            if(projection.NbPoints()&&projection.LowerDistance()<=
                    std::max(1.0e-6,2.*tolerance)) {
                covered=true;
                break;
            }
        }
        if(!covered)return false;
    }
    return true;
}

bool same_face_domain(const TopoDS_Face& first,const TopoDS_Face& second,
        double tolerance) {
    const auto first_parameters=interior_parameters(first,tolerance);
    const auto second_parameters=interior_parameters(second,tolerance);
    if(!first_parameters||!second_parameters)return false;
    BRepAdaptor_Surface first_surface(first,true),second_surface(second,true);
    gp_Pnt first_point,second_point;
    gp_Vec first_u,first_v,second_u,second_v;
    first_surface.D1(first_parameters->first,first_parameters->second,
        first_point,first_u,first_v);
    second_surface.D1(second_parameters->first,second_parameters->second,
        second_point,second_u,second_v);
    auto first_normal=first_u.Crossed(first_v);
    auto second_normal=second_u.Crossed(second_v);
    if(first_normal.SquareMagnitude()<=1.0e-18||
       second_normal.SquareMagnitude()<=1.0e-18||
       first_point.Distance(second_point)>std::max(1.0e-6,2.*tolerance))
        return false;
    first_normal.Normalize();second_normal.Normalize();
    if(first.Orientation()==TopAbs_REVERSED)first_normal.Reverse();
    if(second.Orientation()==TopAbs_REVERSED)second_normal.Reverse();
    if(first_normal.Angle(second_normal)>1.0e-9)return false;
    const double first_area=face_area(first);
    const double second_area=face_area(second);
    if(std::abs(first_area-second_area)>
            std::max(tolerance*tolerance,
                std::max(first_area,second_area)*1.0e-10))
        return false;
    GProp_GProps first_properties,second_properties;
    BRepGProp::SurfaceProperties(first,first_properties);
    BRepGProp::SurfaceProperties(second,second_properties);
    if(first_properties.CentreOfMass().Distance(
            second_properties.CentreOfMass())>std::max(1.0e-6,2.*tolerance))
        return false;
    for(TopExp_Explorer edge(first,TopAbs_EDGE);edge.More();edge.Next())
        if(!edge_is_covered_by_face(
                TopoDS::Edge(edge.Current()),second,tolerance))
            return false;
    for(TopExp_Explorer edge(second,TopAbs_EDGE);edge.More();edge.Next())
        if(!edge_is_covered_by_face(
                TopoDS::Edge(edge.Current()),first,tolerance))
            return false;
    return true;
}

template <typename Source>
auto ordered_references(
        const std::set<int>& sources,
        const std::vector<Source>& faces) {
    using Reference=typename std::remove_cvref_t<
        decltype(faces.front().references)>::value_type;
    struct Entry {
        std::size_t history_index{};
        Reference reference;
    };
    std::vector<Entry> entries;
    for(const int source:sources)
        for(const auto& reference:faces.at(
                static_cast<std::size_t>(source)).references)
            entries.push_back({faces.at(static_cast<std::size_t>(source))
                .history_index,reference});
    std::ranges::sort(entries,[](const Entry& left,const Entry& right) {
        return std::tuple{left.history_index,reference_key(left.reference)}<
            std::tuple{right.history_index,reference_key(right.reference)};
    });
    std::vector<Reference> result;
    for(auto& entry:entries)
        if(std::ranges::none_of(result,[&](const auto& current) {
                return current==entry.reference;
            }))
            result.push_back(std::move(entry.reference));
    return result;
}

gp_Pnt interior_point(const TopoDS_Solid& solid,
        const TopoDS_Shell& shell,double tolerance) {
    const std::array<double,4> offsets{
        std::max(1.0e-3,1000.*tolerance),
        std::max(1.0e-4,100.*tolerance),
        std::max(1.0e-5,10.*tolerance),
        std::max(1.0e-6,2.*tolerance)};
    for(TopExp_Explorer explorer(shell,TopAbs_FACE);explorer.More();
            explorer.Next()) {
        const auto face=TopoDS::Face(explorer.Current());
        const auto parameters=interior_parameters(face,tolerance);
        if(!parameters)continue;
        BRepAdaptor_Surface surface(face,true);
        gp_Pnt point;
        gp_Vec derivative_u,derivative_v;
        surface.D1(parameters->first,parameters->second,point,
            derivative_u,derivative_v);
        auto normal=derivative_u.Crossed(derivative_v);
        if(normal.SquareMagnitude()<=1.0e-18)continue;
        normal.Normalize();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        // Prefer a point well away from the boundary, but back off for thin
        // regions. Near-boundary classification is not reliable enough to
        // decide whether a small closed shell represents material or a void.
        for(const double offset:offsets) {
            const auto candidate=point.Translated(-normal*offset);
            BRepClass3d_SolidClassifier classifier(
                solid,candidate,tolerance);
            if(classifier.State()==TopAbs_IN)return candidate;
        }
    }
    throw std::runtime_error(
        "Boundary union could not sample a closed shell interior");
}

TopoDS_Shape assemble_nested_shells(
        const TopTools_IndexedMapOfShape& shells,double tolerance,
        const std::function<bool(const gp_Pnt&)>* material_at=nullptr,
        bool preserve_input_orientation=false) {
    struct Region {
        TopoDS_Shell shell;
        TopoDS_Solid finite_solid;
        gp_Pnt interior;
        Bnd_Box bounds;
        double bounds_volume{};
        int parent{-1};
        int depth{-1};
        bool material_inside{};
    };
    std::vector<Region> regions;
    regions.reserve(static_cast<std::size_t>(shells.Extent()));
    for(int index=1;index<=shells.Extent();++index) {
        BRepBuilderAPI_MakeSolid builder;
        builder.Add(TopoDS::Shell(shells(index)));
        auto finite=builder.Solid();
        if(!BRepLib::OrientClosedSolid(finite))
            throw std::runtime_error(
                "Boundary union produced a non-orientable shell");
        TopExp_Explorer oriented_shell(finite,TopAbs_SHELL);
        if(!oriented_shell.More())
            throw std::runtime_error(
                "Boundary union lost an oriented shell");
        Region region;
        region.shell=material_at||preserve_input_orientation
            ?TopoDS::Shell(shells(index))
            :TopoDS::Shell(oriented_shell.Current());
        region.finite_solid=finite;
        region.interior=interior_point(finite,
            TopoDS::Shell(oriented_shell.Current()),tolerance);
        BRepBndLib::Add(region.shell,region.bounds,true);
        Standard_Real x_min{},y_min{},z_min{},x_max{},y_max{},z_max{};
        region.bounds.Get(x_min,y_min,z_min,x_max,y_max,z_max);
        region.bounds_volume=(x_max-x_min)*(y_max-y_min)*(z_max-z_min);
        regions.push_back(std::move(region));
    }

    // Closed shells cannot cross after sewing. The smallest finite region that
    // contains a shell is therefore its direct parent. Even nesting depths are
    // material islands; odd depths are voids owned by their direct parent.
    for(std::size_t child=0;child<regions.size();++child) {
        double parent_volume=std::numeric_limits<double>::infinity();
        for(std::size_t candidate=0;candidate<regions.size();++candidate) {
            if(candidate==child||
               regions[candidate].bounds_volume<=
                    regions[child].bounds_volume||
               regions[candidate].bounds_volume>=parent_volume||
               regions[candidate].bounds.IsOut(regions[child].interior))
                continue;
            BRepClass3d_SolidClassifier classifier(
                regions[candidate].finite_solid,
                regions[child].interior,tolerance);
            if(classifier.State()==TopAbs_IN||
               classifier.State()==TopAbs_ON) {
                regions[child].parent=static_cast<int>(candidate);
                parent_volume=regions[candidate].bounds_volume;
            }
        }
    }
    const auto depth_of=[&](auto&& self,int index)->int {
        auto& region=regions[static_cast<std::size_t>(index)];
        if(region.depth>=0)return region.depth;
        int depth=0;
        int parent=region.parent;
        for(std::size_t guard=0;parent>=0&&guard<=regions.size();++guard) {
            ++depth;
            parent=regions[static_cast<std::size_t>(parent)].parent;
            if(guard==regions.size())
                throw std::runtime_error(
                    "Boundary union shell nesting is cyclic");
        }
        region.depth=depth;
        return depth;
    };

    std::vector<TopoDS_Solid> material_solids;
    for(std::size_t region=0;region<regions.size();++region) {
        const int depth=depth_of(depth_of,static_cast<int>(region));
        regions[region].material_inside=material_at
            ?(*material_at)(regions[region].interior)
            :depth%2==0;
    }
    for(std::size_t outer=0;outer<regions.size();++outer) {
        if(!regions[outer].material_inside)continue;
        BRepBuilderAPI_MakeSolid builder;
        builder.Add(regions[outer].shell);
        for(std::size_t child=0;child<regions.size();++child)
            if(regions[child].parent==static_cast<int>(outer)&&
               !regions[child].material_inside) {
                const auto child_shell=material_at||preserve_input_orientation
                    ?regions[child].shell
                    :TopoDS::Shell(regions[child].shell.Reversed());
                builder.Add(child_shell);
            }
        auto solid=builder.Solid();
        if(!BRepCheck_Analyzer(solid).IsValid())
            throw std::runtime_error(
                "Boundary union could not assemble nested shells");
        material_solids.push_back(std::move(solid));
    }
    if(material_solids.empty())
        throw std::runtime_error(
            "Boundary union did not produce a material region");
    if(material_solids.size()==1)return material_solids.front();
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for(const auto& solid:material_solids)builder.Add(compound,solid);
    return compound;
}

} // namespace

static OcctBoundaryUnion occt_boundary_composition_impl(
        std::span<const OcctBoundarySolid> solids,
        std::span<const std::uint8_t> additions,double tolerance) {
    if(solids.empty())
        throw std::invalid_argument("Boundary union requires at least one solid");
    if(!additions.empty()&&additions.size()!=solids.size())
        throw std::invalid_argument(
            "Boundary composition signs must cover every input solid");
    if(!std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Boundary union tolerance must be finite and positive");
    const bool signed_composition=!additions.empty()&&
        std::ranges::any_of(additions,[](std::uint8_t addition) {
            return addition==0;
        });

    // A feature tool may be a compound of several solids. They are separate
    // material regions even though they share one authored operation and its
    // references. Treating the compound as one classification operand hides
    // intersections between its members and can retain internal faces. Split
    // only the derived operand here; the copied face metadata keeps the
    // authored parent and history age unchanged.
    std::vector<OcctBoundarySolid> operands;
    std::vector<std::uint8_t> operand_additions;
    for(std::size_t input_index=0;input_index<solids.size();++input_index) {
        const auto& input=solids[input_index];
        if(input.shape.IsNull()||
           !TopExp_Explorer(input.shape,TopAbs_SOLID).More())
            throw std::invalid_argument(
                "Boundary union inputs must contain solids");
        TopTools_IndexedMapOfShape input_faces,input_solids;
        TopExp::MapShapes(input.shape,TopAbs_FACE,input_faces);
        TopExp::MapShapes(input.shape,TopAbs_SOLID,input_solids);
        if(input_faces.Extent()!=static_cast<int>(input.faces.size()))
            throw std::invalid_argument(
                "Boundary union faces must cover each input solid exactly");
        std::vector<const OcctBoundaryFace*> metadata(
            static_cast<std::size_t>(input_faces.Extent())+1);
        for(const auto& face:input.faces) {
            const int index=input_faces.FindIndex(face.shape);
            if(!index||metadata[static_cast<std::size_t>(index)])
                throw std::invalid_argument(
                    "Boundary union input face metadata is not unique");
            metadata[static_cast<std::size_t>(index)]=&face;
        }
        std::vector<int> occurrences(metadata.size());
        for(int solid=1;solid<=input_solids.Extent();++solid) {
            OcctBoundarySolid operand;
            operand.shape=input_solids(solid);
            TopTools_IndexedMapOfShape member_faces;
            TopExp::MapShapes(operand.shape,TopAbs_FACE,member_faces);
            operand.faces.reserve(
                static_cast<std::size_t>(member_faces.Extent()));
            for(int face=1;face<=member_faces.Extent();++face) {
                const int source=input_faces.FindIndex(member_faces(face));
                if(!source||!metadata[static_cast<std::size_t>(source)])
                    throw std::invalid_argument(
                        "Boundary union solid member lost face metadata");
                auto member=*metadata[static_cast<std::size_t>(source)];
                member.shape=TopoDS::Face(member_faces(face));
                operand.faces.push_back(std::move(member));
                ++occurrences[static_cast<std::size_t>(source)];
            }
            operands.push_back(std::move(operand));
            operand_additions.push_back(additions.empty()?1:
                additions[input_index]);
        }
        if(std::ranges::any_of(occurrences.begin()+1,occurrences.end(),
                [](int count) {return count!=1;}))
            throw std::invalid_argument(
                "Boundary union does not accept shared faces between solid members");
    }

    std::vector<OcctBoundarySolid::Edge> source_edges;
    std::vector<OcctBoundarySolid::Vertex> source_vertices;
    for(const auto& input:solids) {
        TopTools_IndexedMapOfShape edges,vertices;
        TopExp::MapShapes(input.shape,TopAbs_EDGE,edges);
        TopExp::MapShapes(input.shape,TopAbs_VERTEX,vertices);
        for(const auto& edge:input.edges) {
            if(edge.shape.IsNull()||!edges.Contains(edge.shape)||
               edge.references.empty()||
               std::ranges::any_of(edge.references,[](const auto& reference) {
                   return !reference.valid()||
                       !reference.instance_path.empty();
               }))
                throw std::invalid_argument(
                    "Boundary union edges require local stable references");
            source_edges.push_back(edge);
        }
        for(const auto& vertex:input.vertices) {
            if(vertex.shape.IsNull()||!vertices.Contains(vertex.shape)||
               vertex.references.empty()||
               std::ranges::any_of(vertex.references,
                    [](const auto& reference) {
                        return !reference.valid()||
                            !reference.instance_path.empty();
                    }))
                throw std::invalid_argument(
                    "Boundary union vertices require local stable references");
            source_vertices.push_back(vertex);
        }
    }

    OcctBoundaryUnion result;
    std::vector<OcctBoundaryFace> source_faces;
    std::vector<int> source_solid;
    for(std::size_t solid=0;solid<operands.size();++solid) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(operands[solid].shape,TopAbs_FACE,members);
        if(members.Extent()!=static_cast<int>(operands[solid].faces.size()))
            throw std::invalid_argument(
                "Boundary union faces must cover each input solid exactly");
        for(const auto& face:operands[solid].faces) {
            if(face.shape.IsNull()||!members.Contains(face.shape)||
               face.references.empty()||
               std::ranges::any_of(face.references,[](const auto& reference) {
                   return !reference.valid()||
                       !reference.instance_path.empty();
               }))
                throw std::invalid_argument(
                    "Boundary union faces require local stable references");
            source_faces.push_back(face);
            source_solid.push_back(static_cast<int>(solid));
        }
    }
    result.metrics.source_faces=source_faces.size();

    const auto graph_start=Clock::now();
    std::vector<OcctBoundaryFace> preceding;
    preceding.reserve(source_faces.size());
    preceding.insert(preceding.end(),operands.front().faces.begin(),
        operands.front().faces.end());
    for(std::size_t solid=1;solid<operands.size();++solid) {
        auto graph=occt_boundary_intersection_graph(
            preceding,operands[solid].faces,tolerance);
        result.curves.insert(result.curves.end(),
            std::make_move_iterator(graph.curves.begin()),
            std::make_move_iterator(graph.curves.end()));
        preceding.insert(preceding.end(),operands[solid].faces.begin(),
            operands[solid].faces.end());
    }
    result.metrics.intersection_curves=result.curves.size();
    result.metrics.graph_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-graph_start).count();

    TopTools_IndexedMapOfShape face_map;
    for(const auto& face:source_faces)face_map.Add(face.shape);
    if(face_map.Extent()!=static_cast<int>(source_faces.size()))
        throw std::invalid_argument(
            "Boundary union source faces must have unique topology");
    std::vector<std::vector<const OcctBoundaryCurve*>> face_curves(
        static_cast<std::size_t>(face_map.Extent())+1);
    std::vector<std::set<int>> coplanar_peers(
        static_cast<std::size_t>(face_map.Extent()));
    for(const auto& curve:result.curves) {
        const int first=face_map.FindIndex(curve.first_face);
        const int second=face_map.FindIndex(curve.second_face);
        if(!first||!second)
            throw std::runtime_error(
                "Boundary union graph lost a parent face");
        face_curves[static_cast<std::size_t>(first)].push_back(&curve);
        face_curves[static_cast<std::size_t>(second)].push_back(&curve);
        if(curve.kind==OcctBoundaryCurveKind::Coplanar) {
            coplanar_peers[static_cast<std::size_t>(first-1)].insert(second-1);
            coplanar_peers[static_cast<std::size_t>(second-1)].insert(first-1);
        }
    }

    struct Patch {
        TopoDS_Face face;
        std::set<int> sources;
    };
    std::vector<Patch> patches;
    const auto split_start=Clock::now();
    for(int face_index=1;face_index<=face_map.Extent();++face_index) {
        const auto face=TopoDS::Face(face_map(face_index));
        const auto& curves=face_curves[static_cast<std::size_t>(face_index)];
        NCollection_List<TopoDS_Shape> arguments,split_edges;
        arguments.Append(face);
        for(const auto* curve:curves) {
            if(curve_is_face_boundary(*curve,face,tolerance)) {
                ++result.metrics.boundary_curves_skipped;
                continue;
            }
            BRepBuilderAPI_MakeEdge edge(curve->curve,
                curve->first_parameter,curve->last_parameter);
            if(edge.IsDone())split_edges.Append(edge.Edge());
        }
        if(curves.empty()||split_edges.IsEmpty()) {
            patches.push_back({face,{face_index-1}});
            continue;
        }
        ++result.metrics.split_faces;
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(arguments);
        splitter.SetTools(split_edges);
        splitter.SetNonDestructive(true);
        splitter.SetRunParallel(false);
        splitter.SetFuzzyValue(tolerance);
        splitter.Build();
        if(!splitter.IsDone()||splitter.Shape().IsNull())
            throw std::runtime_error("Boundary union face split failed");
        TopTools_IndexedMapOfShape split_faces;
        TopExp::MapShapes(splitter.Shape(),TopAbs_FACE,split_faces);
        result.metrics.split_patches+=split_faces.Extent();
        for(int patch=1;patch<=split_faces.Extent();++patch)
            patches.push_back({TopoDS::Face(split_faces(patch)),
                {face_index-1}});
    }
    result.metrics.split_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-split_start).count();

    std::vector<Bnd_Box> solid_bounds(operands.size());
    std::vector<std::unique_ptr<IntCurvesFace_ShapeIntersector>>
        ray_intersectors;
    std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>>
        solid_classifiers;
    ray_intersectors.reserve(operands.size());
    solid_classifiers.reserve(operands.size());
    for(std::size_t solid=0;solid<operands.size();++solid) {
        BRepBndLib::Add(operands[solid].shape,solid_bounds[solid],true);
        auto intersector=std::make_unique<IntCurvesFace_ShapeIntersector>();
        intersector->Load(operands[solid].shape,tolerance);
        ray_intersectors.push_back(std::move(intersector));
        solid_classifiers.push_back(
            std::make_unique<BRepClass3d_SolidClassifier>(
                operands[solid].shape));
    }
    const std::array<gp_Dir,4> directions{
        gp_Dir(1.,.3713906763541037,.1278314382941072),
        gp_Dir(.2174811048921037,1.,.4393819273014491),
        gp_Dir(.4938721098301713,.1827391049281037,1.),
        gp_Dir(-.7310948127301942,1.,.2837190482719037)};
    const auto ray_inside=[&](std::size_t solid,const gp_Pnt& point)
            ->std::optional<bool> {
        auto& intersector=*ray_intersectors[solid];
        for(const auto& direction:directions) {
            intersector.PerformNearest(gp_Lin(point,direction),
                std::max(2.*tolerance,1.0e-8),1.0e100);
            if(!intersector.IsDone())continue;
            if(intersector.NbPnt()==0)return false;
            const auto transition=intersector.Transition(1);
            if(transition==IntCurveSurface_Tangent)continue;
            return transition==IntCurveSurface_Out;
        }
        return std::nullopt;
    };
    const auto inside_solid=[&](std::size_t solid,const gp_Pnt& point) {
        if(solid_bounds[solid].IsOut(point))return false;
        ++result.metrics.containment_queries;
        if(signed_composition) {
            solid_classifiers[solid]->Perform(point,tolerance);
            return solid_classifiers[solid]->State()==TopAbs_IN||
                solid_classifiers[solid]->State()==TopAbs_ON;
        }
        auto inside=ray_inside(solid,point);
        if(!inside) {
            ++result.metrics.ray_fallbacks;
            solid_classifiers[solid]->Perform(point,tolerance);
            inside=solid_classifiers[solid]->State()==TopAbs_IN||
                solid_classifiers[solid]->State()==TopAbs_ON;
        }
        return *inside;
    };
    const std::function<bool(const gp_Pnt&)> material_at=
        [&](const gp_Pnt& point) {
            bool positive{};
            for(std::size_t solid=0;solid<operands.size();++solid) {
                if(!inside_solid(solid,point))continue;
                if(!operand_additions[solid])return false;
                positive=true;
            }
            return positive;
        };

    std::vector<Patch> retained;
    std::vector<Bnd_Box> retained_bounds;
    retained.reserve(patches.size());
    retained_bounds.reserve(patches.size());
    const auto classify_start=Clock::now();
    for(auto& patch:patches) {
        const auto parameters=interior_parameters(patch.face,tolerance);
        if(!parameters)
            throw std::runtime_error(
                "Boundary union could not sample a split face");
        BRepAdaptor_Surface surface(patch.face,true);
        gp_Pnt point;
        gp_Vec derivative_u,derivative_v;
        surface.D1(parameters->first,parameters->second,point,
            derivative_u,derivative_v);
        auto normal=derivative_u.Crossed(derivative_v);
        if(normal.SquareMagnitude()<=1.0e-18)
            throw std::runtime_error(
                "Boundary union sampled a singular face normal");
        normal.Normalize();
        if(patch.face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        const double sample_offset=std::max(1.0e-5,10.*tolerance);
        const auto outward=point.Translated(normal*sample_offset);
        const int own_solid=source_solid.at(
            static_cast<std::size_t>(*patch.sources.begin()));
        bool hidden=false;
        if(signed_composition) {
            const auto inward=point.Translated(-normal*sample_offset);
            const bool material_outward=material_at(outward);
            const bool material_inward=material_at(inward);
            if(material_outward==material_inward)hidden=true;
            else if(material_outward)patch.face.Reverse();
        } else {
            for(std::size_t solid=0;solid<operands.size();++solid) {
                if(static_cast<int>(solid)==own_solid||
                   solid_bounds[solid].IsOut(outward))
                    continue;
                if(inside_solid(solid,outward)) {
                    hidden=true;
                    break;
                }
            }
        }
        if(hidden) {
            ++result.metrics.discarded_internal_patches;
            continue;
        }
        Bnd_Box patch_bounds;
        BRepBndLib::Add(patch.face,patch_bounds,true);
        bool duplicate=false;
        for(std::size_t index=0;index<retained.size();++index) {
            auto& candidate=retained[index];
            if(source_solid.at(static_cast<std::size_t>(
                    *candidate.sources.begin()))==own_solid)
                continue;
            const bool has_coplanar_parent=std::ranges::any_of(
                patch.sources,[&](const int source) {
                    return std::ranges::any_of(candidate.sources,
                        [&](const int other) {
                            return coplanar_peers[
                                static_cast<std::size_t>(source)].contains(
                                    other);
                        });
                });
            if(!has_coplanar_parent)continue;
            if(retained_bounds[index].IsOut(patch_bounds))continue;
            if(!same_face_domain(candidate.face,patch.face,tolerance))
                continue;
            candidate.sources.insert(patch.sources.begin(),
                patch.sources.end());
            duplicate=true;
            break;
        }
        if(!duplicate) {
            retained.push_back(std::move(patch));
            retained_bounds.push_back(std::move(patch_bounds));
        }
    }
    result.metrics.retained_patches=retained.size();
    result.metrics.classify_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-classify_start).count();

    const auto sew_start=Clock::now();
    BRepBuilderAPI_Sewing sewing(tolerance,true,true,true,false);
    for(const auto& patch:retained)sewing.Add(patch.face);
    sewing.Perform();
    const auto sewed=sewing.SewedShape();
    result.metrics.free_edges=sewing.NbFreeEdges();
    result.metrics.multiple_edges=sewing.NbMultipleEdges();
    if(sewed.IsNull()||result.metrics.free_edges||
       result.metrics.multiple_edges) {
        std::ostringstream message;
        message<<"Boundary union did not produce manifold closed shells"
            <<" (free="<<result.metrics.free_edges
            <<", multiple="<<result.metrics.multiple_edges
            <<", retained="<<result.metrics.retained_patches
            <<", curves="<<result.metrics.intersection_curves
            <<", split="<<result.metrics.split_patches;
        for(int index=1;index<=std::min(8,sewing.NbFreeEdges());++index) {
            const BRepAdaptor_Curve curve(sewing.FreeEdge(index));
            const auto first=curve.Value(curve.FirstParameter());
            const auto last=curve.Value(curve.LastParameter());
            message<<", edge"<<index<<'='<<static_cast<int>(curve.GetType())
                <<':'<<first.X()<<','<<first.Y()<<','<<first.Z()<<'-'
                <<last.X()<<','<<last.Y()<<','<<last.Z();
        }
        message<<')';
        throw std::runtime_error(message.str());
    }
    TopTools_IndexedMapOfShape sewed_shells,sewed_faces;
    TopExp::MapShapes(sewed,TopAbs_SHELL,sewed_shells);
    TopExp::MapShapes(sewed,TopAbs_FACE,sewed_faces);
    result.metrics.sewed_shells=sewed_shells.Extent();
    std::vector<std::set<int>> sewed_sources(
        static_cast<std::size_t>(sewed_faces.Extent())+1);
    for(const auto& patch:retained) {
        const auto& image=sewing.IsModified(patch.face)
            ?sewing.Modified(patch.face)
            :static_cast<const TopoDS_Shape&>(patch.face);
        for(TopExp_Explorer face(image,TopAbs_FACE);face.More();face.Next()) {
            const int target=sewed_faces.FindIndex(face.Current());
            if(target)sewed_sources[static_cast<std::size_t>(target)].insert(
                patch.sources.begin(),patch.sources.end());
        }
    }
    TopoDS_Shape grouped_shape;
    if(signed_composition) {
        grouped_shape=assemble_nested_shells(sewed_shells,tolerance,
            &material_at,true);
    } else {
        BRepBuilderAPI_MakeSolid solid_builder;
        for(int shell=1;shell<=sewed_shells.Extent();++shell)
            solid_builder.Add(TopoDS::Shell(sewed_shells(shell)));
        auto grouped=solid_builder.Solid();
        BRepLib::OrientClosedSolid(grouped);
        grouped_shape=grouped;
        if(!BRepCheck_Analyzer(grouped_shape).IsValid())
            grouped_shape=assemble_nested_shells(sewed_shells,tolerance);
    }
    if(grouped_shape.IsNull()||
       !BRepCheck_Analyzer(grouped_shape).IsValid())
        throw std::runtime_error(
            "Boundary union could not assemble valid solids");
    TopTools_IndexedMapOfShape grouped_faces;
    TopExp::MapShapes(grouped_shape,TopAbs_FACE,grouped_faces);
    if(grouped_faces.Extent()!=sewed_faces.Extent())
        throw std::runtime_error(
            "Boundary union shell assembly changed face provenance");
    result.metrics.sew_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-sew_start).count();

    std::vector<std::vector<FaceReference>> sewed_references(
        static_cast<std::size_t>(sewed_faces.Extent())+1);
    std::vector<std::size_t> sewed_history(
        static_cast<std::size_t>(sewed_faces.Extent())+1,
        std::numeric_limits<std::size_t>::max());
    for(int face=1;face<=sewed_faces.Extent();++face) {
        auto& references=sewed_references[static_cast<std::size_t>(face)];
        const auto& sources=sewed_sources[static_cast<std::size_t>(face)];
        references=ordered_references(sources,source_faces);
        for(const int source:sources)
            sewed_history[static_cast<std::size_t>(face)]=std::min(
                sewed_history[static_cast<std::size_t>(face)],
                source_faces[static_cast<std::size_t>(source)].history_index);
        if(references.empty())
            throw std::runtime_error(
                "Boundary union lost a sewed face reference");
    }

    const auto unify_start=Clock::now();
    ShapeUpgrade_UnifySameDomain probe(
        grouped_shape,true,true,false);
    probe.SetLinearTolerance(tolerance);
    probe.SetAngularTolerance(1.0e-9);
    probe.Build();
    if(probe.Shape().IsNull())
        throw std::runtime_error("Boundary union unification probe failed");
    TopTools_IndexedMapOfShape probe_faces,probe_edges;
    TopExp::MapShapes(probe.Shape(),TopAbs_FACE,probe_faces);
    TopExp::MapShapes(probe.Shape(),TopAbs_EDGE,probe_edges);
    result.metrics.unprotected_faces=
        static_cast<std::size_t>(probe_faces.Extent());
    result.metrics.unprotected_edges=
        static_cast<std::size_t>(probe_edges.Extent());
    const auto probe_edge_survives=[&](const TopoDS_Shape& edge) {
        if(probe_edges.Contains(edge))return true;
        const auto history=probe.History();
        if(history.IsNull())return false;
        for(const auto* descendants:{&history->Modified(edge),
                &history->Generated(edge)})
            for(TopTools_ListIteratorOfListOfShape descendant(
                    *descendants);descendant.More();descendant.Next())
                if(descendant.Value().ShapeType()==TopAbs_EDGE&&
                   probe_edges.Contains(descendant.Value()))
                    return true;
        return false;
    };
    ShapeUpgrade_UnifySameDomain unifier(
        grouped_shape,true,true,false);
    unifier.SetLinearTolerance(tolerance);
    unifier.SetAngularTolerance(1.0e-9);
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(grouped_shape,TopAbs_EDGE,
        TopAbs_FACE,edge_faces);
    for(int edge=1;edge<=edge_faces.Extent();++edge) {
        const auto& adjacent=edge_faces.FindFromIndex(edge);
        if(adjacent.Extent()!=2)continue;
        TopTools_ListIteratorOfListOfShape iterator(adjacent);
        const auto first_face=TopoDS::Face(iterator.Value());
        const int first_index=sewed_faces.FindIndex(first_face);
        iterator.Next();
        const auto second_face=TopoDS::Face(iterator.Value());
        const int second_index=sewed_faces.FindIndex(second_face);
        bool may_merge=first_index&&second_index;
        if(may_merge) {
            const auto& first_references=sewed_references[
                static_cast<std::size_t>(first_index)];
            const auto& second_references=sewed_references[
                static_cast<std::size_t>(second_index)];
            if(std::ranges::any_of(first_references,
                    [&](const auto& first) {
                        return std::ranges::find(second_references,first)!=
                            second_references.end();
                    })) {
                may_merge=true;
            } else if(std::ranges::any_of(first_references,
                    [&](const auto& first) {
                        return std::ranges::any_of(second_references,
                            [&](const auto& second) {
                                return first.surface_result==
                                        second.surface_result&&
                                    first.sheet_role==second.sheet_role;
                            });
                    })) {
                const BRepAdaptor_Surface first_surface(first_face),
                    second_surface(second_face);
                may_merge=first_surface.GetType()==GeomAbs_Plane&&
                    second_surface.GetType()==GeomAbs_Plane;
                if(may_merge) {
                    auto first_normal=
                        first_surface.Plane().Axis().Direction();
                    auto second_normal=
                        second_surface.Plane().Axis().Direction();
                    if(!first_surface.Plane().Position().Direct())
                        first_normal.Reverse();
                    if(!second_surface.Plane().Position().Direct())
                        second_normal.Reverse();
                    if(first_face.Orientation()==TopAbs_REVERSED)
                        first_normal.Reverse();
                    if(second_face.Orientation()==TopAbs_REVERSED)
                        second_normal.Reverse();
                    may_merge=first_normal.Angle(second_normal)<=1.0e-9&&
                        first_surface.Plane().Distance(
                            second_surface.Plane().Location())<=tolerance;
                }
            } else may_merge=false;
        }
        if(!may_merge&&!probe_edge_survives(edge_faces.FindKey(edge))) {
            unifier.KeepShape(edge_faces.FindKey(edge));
            ++result.metrics.protected_identity_edges;
        }
    }
    unifier.Build();
    result.shape=unifier.Shape();
    if(result.shape.IsNull()||
       !BRepCheck_Analyzer(result.shape).IsValid())
        throw std::runtime_error(
            "Boundary union unification produced an invalid shape");
    const auto history=unifier.History();
    TopTools_IndexedMapOfShape result_solids;
    TopExp::MapShapes(result.shape,TopAbs_SOLID,result_solids);
    if(result_solids.Extent()==1&&result.shape.ShapeType()==TopAbs_SOLID) {
        auto finite=TopoDS::Solid(result.shape);
        BRepLib::OrientClosedSolid(finite);
        result.shape=finite;
    } else if(result_solids.Extent()>0) {
        BRep_Builder builder;
        TopoDS_Compound finite;
        builder.MakeCompound(finite);
        for(int solid=1;solid<=result_solids.Extent();++solid) {
            auto member=TopoDS::Solid(result_solids(solid));
            BRepLib::OrientClosedSolid(member);
            builder.Add(finite,member);
        }
        result.shape=finite;
    }

    TopTools_IndexedMapOfShape final_faces;
    TopExp::MapShapes(result.shape,TopAbs_FACE,final_faces);
    std::vector<std::set<int>> final_sources(
        static_cast<std::size_t>(final_faces.Extent())+1);
    for(int face=1;face<=sewed_faces.Extent();++face) {
        std::vector<TopoDS_Shape> descendants;
        if(!history.IsNull())
            for(const auto* list:{&history->Modified(sewed_faces(face)),
                    &history->Generated(sewed_faces(face))})
                for(TopTools_ListIteratorOfListOfShape descendant(*list);
                        descendant.More();descendant.Next())
                    if(descendant.Value().ShapeType()==TopAbs_FACE)
                        descendants.push_back(descendant.Value());
        if(descendants.empty()&&(history.IsNull()||
                !history->IsRemoved(sewed_faces(face))))
            descendants.push_back(sewed_faces(face));
        for(const auto& descendant:descendants) {
            const int target=final_faces.FindIndex(descendant);
            if(target)final_sources[static_cast<std::size_t>(target)].insert(
                sewed_sources[static_cast<std::size_t>(face)].begin(),
                sewed_sources[static_cast<std::size_t>(face)].end());
        }
    }
    result.faces.reserve(static_cast<std::size_t>(final_faces.Extent()));
    for(int face=1;face<=final_faces.Extent();++face) {
        const auto& sources=final_sources[static_cast<std::size_t>(face)];
        auto references=ordered_references(sources,source_faces);
        if(references.empty())
            throw std::runtime_error(
                "Boundary union lost a final face reference");
        std::size_t history_index=std::numeric_limits<std::size_t>::max();
        for(const int source:sources)
            history_index=std::min(history_index,
                source_faces[static_cast<std::size_t>(source)].history_index);
        result.faces.push_back({TopoDS::Face(final_faces(face)),
            std::move(references),history_index});
    }

    TopTools_IndexedMapOfShape final_edges,final_vertices;
    TopExp::MapShapes(result.shape,TopAbs_EDGE,final_edges);
    TopExp::MapShapes(result.shape,TopAbs_VERTEX,final_vertices);
    std::vector<Bnd_Box> source_edge_bounds(source_edges.size());
    for(std::size_t source=0;source<source_edges.size();++source)
        BRepBndLib::Add(source_edges[source].shape,
            source_edge_bounds[source],true);
    result.edges.reserve(static_cast<std::size_t>(final_edges.Extent()));
    for(int edge=1;edge<=final_edges.Extent();++edge) {
        const auto shape=TopoDS::Edge(final_edges(edge));
        Bnd_Box bounds;
        BRepBndLib::Add(shape,bounds,true);
        std::set<int> sources;
        for(std::size_t source=0;source<source_edges.size();++source)
            if(!bounds.IsOut(source_edge_bounds[source])&&
               edge_is_covered_by_edge(shape,source_edges[source].shape,
                    tolerance))
                sources.insert(static_cast<int>(source));
        auto references=ordered_references(sources,source_edges);
        std::size_t history_index=std::numeric_limits<std::size_t>::max();
        for(const int source:sources)
            history_index=std::min(history_index,
                source_edges[static_cast<std::size_t>(source)]
                    .history_index);
        result.edges.push_back({shape,std::move(references),history_index});
    }

    std::vector<gp_Pnt> source_vertex_points;
    source_vertex_points.reserve(source_vertices.size());
    for(const auto& vertex:source_vertices)
        source_vertex_points.push_back(BRep_Tool::Pnt(vertex.shape));
    result.vertices.reserve(
        static_cast<std::size_t>(final_vertices.Extent()));
    for(int vertex=1;vertex<=final_vertices.Extent();++vertex) {
        const auto shape=TopoDS::Vertex(final_vertices(vertex));
        const auto point=BRep_Tool::Pnt(shape);
        std::set<int> sources;
        for(std::size_t source=0;source<source_vertices.size();++source)
            if(point.Distance(source_vertex_points[source])<=
                    std::max(1.0e-6,2.*tolerance))
                sources.insert(static_cast<int>(source));
        auto references=ordered_references(sources,source_vertices);
        std::size_t history_index=std::numeric_limits<std::size_t>::max();
        for(const int source:sources)
            history_index=std::min(history_index,
                source_vertices[static_cast<std::size_t>(source)]
                    .history_index);
        result.vertices.push_back(
            {shape,std::move(references),history_index});
    }
    result.metrics.unify_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-unify_start).count();
    return result;
}

OcctBoundaryUnion occt_boundary_union(
        std::span<const OcctBoundarySolid> solids,double tolerance) {
    return occt_boundary_composition_impl(solids,{},tolerance);
}

OcctBoundaryUnion occt_boundary_composition(
        std::span<const OcctBoundaryRegion> regions,double tolerance) {
    if(regions.empty())
        throw std::invalid_argument(
            "Boundary composition requires at least one region");
    if(std::ranges::none_of(regions,[](const auto& region) {
            return region.adds_material;
        }))
        throw std::invalid_argument(
            "Boundary composition requires a positive material region");
    std::vector<OcctBoundarySolid> solids;
    std::vector<std::uint8_t> additions;
    solids.reserve(regions.size());
    additions.reserve(regions.size());
    for(const auto& region:regions) {
        solids.push_back(region.boundary);
        additions.push_back(region.adds_material?1:0);
    }
    return occt_boundary_composition_impl(solids,additions,tolerance);
}

static OcctBoundaryNamedTopology occt_boundary_named_topology_impl(
        const OcctBoundaryUnion& boundary,
        std::span<const std::string> history_owners,double tolerance,
        std::string_view name_space) {
    if(boundary.shape.IsNull())
        throw std::invalid_argument(
            "Boundary topology naming requires a calculated shape");
    if(!std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Boundary topology naming tolerance must be finite and positive");

    std::map<std::string,std::size_t> owner_history;
    for(std::size_t index=0;index<history_owners.size();++index)
        owner_history.try_emplace(history_owners[index],index);
    const auto reference_history=[&](const auto& reference,
            std::size_t fallback) {
        const auto found=owner_history.find(reference.owner_id);
        return found==owner_history.end()?fallback:found->second;
    };
    const auto generated_owner=[&](std::size_t history_index)
            ->const std::string& {
        if(history_index>=history_owners.size()||
           history_owners[history_index].empty())
            throw std::runtime_error(
                "Boundary topology cannot own a generated identity");
        return history_owners[history_index];
    };

    TopTools_IndexedMapOfShape faces,edges,vertices;
    TopExp::MapShapes(boundary.shape,TopAbs_FACE,faces);
    TopExp::MapShapes(boundary.shape,TopAbs_EDGE,edges);
    TopExp::MapShapes(boundary.shape,TopAbs_VERTEX,vertices);
    if(faces.Extent()!=static_cast<int>(boundary.faces.size())||
       edges.Extent()!=static_cast<int>(boundary.edges.size())||
       vertices.Extent()!=static_cast<int>(boundary.vertices.size()))
        throw std::invalid_argument(
            "Boundary topology naming requires complete provenance");

    std::vector<const OcctBoundaryUnionFace*> face_metadata(
        static_cast<std::size_t>(faces.Extent())+1);
    std::vector<const OcctBoundaryUnionEdge*> edge_metadata(
        static_cast<std::size_t>(edges.Extent())+1);
    std::vector<const OcctBoundaryUnionVertex*> vertex_metadata(
        static_cast<std::size_t>(vertices.Extent())+1);
    for(const auto& face:boundary.faces) {
        const int index=faces.FindIndex(face.shape);
        if(!index||face_metadata[static_cast<std::size_t>(index)])
            throw std::invalid_argument(
                "Boundary topology face provenance is not unique");
        face_metadata[static_cast<std::size_t>(index)]=&face;
    }
    for(const auto& edge:boundary.edges) {
        const int index=edges.FindIndex(edge.shape);
        if(!index||edge_metadata[static_cast<std::size_t>(index)])
            throw std::invalid_argument(
                "Boundary topology edge provenance is not unique");
        edge_metadata[static_cast<std::size_t>(index)]=&edge;
    }
    for(const auto& vertex:boundary.vertices) {
        const int index=vertices.FindIndex(vertex.shape);
        if(!index||vertex_metadata[static_cast<std::size_t>(index)])
            throw std::invalid_argument(
                "Boundary topology vertex provenance is not unique");
        vertex_metadata[static_cast<std::size_t>(index)]=&vertex;
    }

    struct CurveEvent {
        const OcctBoundaryCurve* curve{};
        Bnd_Box bounds;
        std::string token;
        std::size_t latest_history{};
    };
    std::vector<CurveEvent> curve_events;
    curve_events.reserve(boundary.curves.size());
    for(const auto& curve:boundary.curves) {
        std::set<std::string> parents;
        for(const auto& reference:curve.first_parents)
            parents.insert(encoded_reference(reference));
        for(const auto& reference:curve.second_parents)
            parents.insert(encoded_reference(reference));
        if(parents.empty()||curve.curve.IsNull()||
           curve.last_parameter<=curve.first_parameter)continue;
        CurveEvent event;
        event.curve=&curve;
        event.latest_history=std::max(curve.first_history_index,
            curve.second_history_index);
        event.token=std::string(curve.kind==
                OcctBoundaryCurveKind::Coplanar?"coplanar:":"transverse:")+
            encoded_reference_set(parents);
        GeomAdaptor_Curve adaptor(curve.curve,curve.first_parameter,
            curve.last_parameter);
        BndLib_Add3dCurve::Add(adaptor,
            std::max(1.0e-7,tolerance),event.bounds);
        event.bounds.Enlarge(std::max(1.0e-6,2.*tolerance));
        curve_events.push_back(std::move(event));
    }
    std::vector<std::vector<std::size_t>> edge_events(
        static_cast<std::size_t>(edges.Extent())+1);
    for(int edge=1;edge<=edges.Extent();++edge) {
        Bnd_Box bounds;
        BRepBndLib::Add(edges(edge),bounds,true);
        bounds.Enlarge(std::max(1.0e-6,2.*tolerance));
        for(std::size_t event=0;event<curve_events.size();++event)
            if(!bounds.IsOut(curve_events[event].bounds)&&
               edge_is_covered_by_curve(TopoDS::Edge(edges(edge)),
                    *curve_events[event].curve,tolerance))
                edge_events[static_cast<std::size_t>(edge)].push_back(event);
    }

    const auto add_reference_context=[&](std::set<std::string>& context,
            std::size_t& latest,std::string_view role,const auto& references,
            std::size_t fallback) {
        for(const auto& reference:references) {
            context.insert(std::string(role)+encoded_reference(reference));
            latest=std::max(latest,
                reference_history(reference,fallback));
        }
    };
    std::map<std::string,std::size_t> face_parent_counts,
        edge_parent_counts,vertex_parent_counts;
    for(const auto& face:boundary.faces)
        for(const auto& reference:face.references)
            ++face_parent_counts[reference_key(reference)];
    for(const auto& edge:boundary.edges)
        for(const auto& reference:edge.references)
            ++edge_parent_counts[reference_key(reference)];
    for(const auto& vertex:boundary.vertices)
        for(const auto& reference:vertex.references)
            ++vertex_parent_counts[reference_key(reference)];

    TopTools_IndexedDataMapOfShapeListOfShape edge_faces,vertex_faces,
        vertex_edges;
    TopExp::MapShapesAndAncestors(boundary.shape,TopAbs_EDGE,TopAbs_FACE,
        edge_faces);
    TopExp::MapShapesAndAncestors(boundary.shape,TopAbs_VERTEX,TopAbs_FACE,
        vertex_faces);
    TopExp::MapShapesAndAncestors(boundary.shape,TopAbs_VERTEX,TopAbs_EDGE,
        vertex_edges);

    OcctBoundaryNamedTopology result;
    result.faces.reserve(boundary.faces.size());
    std::vector<const OcctBoundaryNamedTopologyEntry<TopoDS_Face,
        FaceReference>*> named_faces(static_cast<std::size_t>(faces.Extent())+1);
    for(int face_index=1;face_index<=faces.Extent();++face_index) {
        const auto& metadata=*face_metadata[
            static_cast<std::size_t>(face_index)];
        if(metadata.references.empty())
            throw std::runtime_error(
                "Boundary topology face has no authored parent");
        const auto& primary_reference=metadata.references.front();
        const auto primary=encoded_reference(primary_reference);
        const bool planar=BRepAdaptor_Surface(
            TopoDS::Face(faces(face_index))).GetType()==GeomAbs_Plane;
        const bool may_inherit=(metadata.references.size()==1||planar)&&
            face_parent_counts.at(reference_key(primary_reference))==1;
        if(may_inherit) {
            result.faces.push_back({TopoDS::Face(faces(face_index)),
                primary_reference,reference_history(primary_reference,
                    metadata.history_index)});
            named_faces[static_cast<std::size_t>(face_index)]=
                &result.faces.back();
            continue;
        }

        std::set<std::string> context;
        std::size_t latest=metadata.history_index;
        add_reference_context(context,latest,"source-face:",
            metadata.references,metadata.history_index);
        for(TopExp_Explorer boundary_edge(faces(face_index),TopAbs_EDGE);
                boundary_edge.More();boundary_edge.Next()) {
            const int edge_index=edges.FindIndex(boundary_edge.Current());
            if(!edge_index)continue;
            const auto& edge=*edge_metadata[
                static_cast<std::size_t>(edge_index)];
            add_reference_context(context,latest,"source-edge:",
                edge.references,edge.history_index);
            for(const auto event:edge_events[
                    static_cast<std::size_t>(edge_index)]) {
                context.insert("curve:"+curve_events[event].token);
                latest=std::max(latest,
                    curve_events[event].latest_history);
            }
            for(TopExp_Explorer endpoint(boundary_edge.Current(),
                    TopAbs_VERTEX);endpoint.More();endpoint.Next()) {
                const int vertex_index=vertices.FindIndex(endpoint.Current());
                if(!vertex_index)continue;
                const auto& vertex=*vertex_metadata[
                    static_cast<std::size_t>(vertex_index)];
                add_reference_context(context,latest,"source-vertex:",
                    vertex.references,vertex.history_index);
            }
            const int adjacency=edge_faces.FindIndex(boundary_edge.Current());
            if(!adjacency)continue;
            for(TopTools_ListIteratorOfListOfShape adjacent(
                    edge_faces.FindFromIndex(adjacency));adjacent.More();
                    adjacent.Next()) {
                if(adjacent.Value().IsSame(faces(face_index)))continue;
                const int adjacent_index=faces.FindIndex(adjacent.Value());
                if(!adjacent_index)continue;
                const auto& adjacent_metadata=*face_metadata[
                    static_cast<std::size_t>(adjacent_index)];
                add_reference_context(context,latest,"adjacent-face:",
                    adjacent_metadata.references,
                    adjacent_metadata.history_index);
            }
        }
        auto reference=primary_reference;
        reference.owner_id=generated_owner(latest);
        reference.semantic_key=direct_boundary_key(
            name_space,"face",primary,context);
        result.faces.push_back({TopoDS::Face(faces(face_index)),
            std::move(reference),latest});
        named_faces[static_cast<std::size_t>(face_index)]=
            &result.faces.back();
    }

    result.edges.reserve(boundary.edges.size());
    std::vector<const OcctBoundaryNamedTopologyEntry<TopoDS_Edge,
        EdgeReference>*> named_edges(static_cast<std::size_t>(edges.Extent())+1);
    for(int edge_index=1;edge_index<=edges.Extent();++edge_index) {
        const auto& metadata=*edge_metadata[
            static_cast<std::size_t>(edge_index)];
        const bool may_inherit=metadata.references.size()==1&&
            edge_parent_counts.at(reference_key(metadata.references.front()))==1;
        if(may_inherit) {
            const auto& reference=metadata.references.front();
            result.edges.push_back({TopoDS::Edge(edges(edge_index)),reference,
                reference_history(reference,metadata.history_index)});
            named_edges[static_cast<std::size_t>(edge_index)]=
                &result.edges.back();
            continue;
        }

        std::set<std::string> context,primary_candidates;
        std::size_t latest=metadata.history_index==
                std::numeric_limits<std::size_t>::max()?0:
            metadata.history_index;
        add_reference_context(context,latest,"source-edge:",
            metadata.references,latest);
        for(const auto& reference:metadata.references)
            primary_candidates.insert(encoded_reference(reference));
        const int adjacency=edge_faces.FindIndex(edges(edge_index));
        if(adjacency)
            for(TopTools_ListIteratorOfListOfShape adjacent(
                    edge_faces.FindFromIndex(adjacency));adjacent.More();
                    adjacent.Next()) {
                const int face_index=faces.FindIndex(adjacent.Value());
                if(!face_index)continue;
                const auto& face=*named_faces[
                    static_cast<std::size_t>(face_index)];
                const auto encoded=encoded_reference(face.reference);
                context.insert("face:"+encoded);
                primary_candidates.insert(encoded);
                latest=std::max(latest,face.history_index);
            }
        for(const auto event:edge_events[
                static_cast<std::size_t>(edge_index)]) {
            context.insert("curve:"+curve_events[event].token);
            latest=std::max(latest,curve_events[event].latest_history);
        }
        std::set<std::string> endpoint_contexts;
        for(TopExp_Explorer endpoint(edges(edge_index),TopAbs_VERTEX);
                endpoint.More();endpoint.Next()) {
            const int vertex_index=vertices.FindIndex(endpoint.Current());
            if(!vertex_index)continue;
            const auto& vertex=*vertex_metadata[
                static_cast<std::size_t>(vertex_index)];
            std::set<std::string> endpoint_context;
            add_reference_context(endpoint_context,latest,"source-vertex:",
                vertex.references,vertex.history_index);
            const int face_adjacency=vertex_faces.FindIndex(
                endpoint.Current());
            if(face_adjacency)
                for(TopTools_ListIteratorOfListOfShape adjacent(
                        vertex_faces.FindFromIndex(face_adjacency));
                        adjacent.More();adjacent.Next()) {
                    const int face_index=faces.FindIndex(adjacent.Value());
                    if(!face_index)continue;
                    const auto& face=*named_faces[
                        static_cast<std::size_t>(face_index)];
                    endpoint_context.insert("face:"+
                        encoded_reference(face.reference));
                    latest=std::max(latest,face.history_index);
                }
            const int edge_adjacency=vertex_edges.FindIndex(
                endpoint.Current());
            if(edge_adjacency)
                for(TopTools_ListIteratorOfListOfShape incident(
                        vertex_edges.FindFromIndex(edge_adjacency));
                        incident.More();incident.Next()) {
                    const int incident_index=edges.FindIndex(
                        incident.Value());
                    if(!incident_index)continue;
                    const auto& incident_metadata=*edge_metadata[
                        static_cast<std::size_t>(incident_index)];
                    add_reference_context(endpoint_context,latest,
                        "source-edge:",incident_metadata.references,
                        incident_metadata.history_index);
                    for(const auto event:edge_events[
                            static_cast<std::size_t>(incident_index)]) {
                        endpoint_context.insert("curve:"+
                            curve_events[event].token);
                        latest=std::max(latest,
                            curve_events[event].latest_history);
                    }
                }
            if(endpoint_context.empty())
                throw std::runtime_error(
                    "Boundary topology edge endpoint has no persisted ancestry");
            endpoint_contexts.insert(
                encoded_reference_set(endpoint_context));
        }
        if(primary_candidates.empty())
            throw std::runtime_error(
                "Boundary topology edge has no persisted ancestry");
        // Distinguish surviving intervals only by their persisted endpoint
        // incidence. Coordinates, length and OCCT enumeration must not rename
        // an edge when a driving dimension moves the same authored topology.
        for(const auto& endpoint_context:endpoint_contexts)
            context.insert("endpoint:"+endpoint_context);
        const auto& primary=*primary_candidates.begin();
        EdgeReference reference{generated_owner(latest),
            direct_boundary_key(name_space,metadata.references.empty()
                ?"intersection-edge":"split-edge",primary,context),{}};
        result.edges.push_back({TopoDS::Edge(edges(edge_index)),
            std::move(reference),latest});
        named_edges[static_cast<std::size_t>(edge_index)]=
            &result.edges.back();
    }

    // One persisted logical edge may survive as several disconnected current
    // fragments. If every colliding generated split has exactly the same one
    // authored parent, retain that parent alias for all fragments. This is not
    // an ambiguity: downstream resolution intentionally addresses their union.
    // New intersections or splits with different/multiple parents remain
    // required to produce distinct ancestry-derived identities below.
    std::set<std::string> shared_edge_aliases;
    std::map<std::string,std::vector<std::size_t>> edge_identity_groups;
    for(std::size_t index=0;index<result.edges.size();++index)
        edge_identity_groups[reference_key(result.edges[index].reference)].
            push_back(index);
    for(const auto& [identity,indices]:edge_identity_groups) {
        static_cast<void>(identity);
        if(indices.size()<2)continue;
        std::optional<EdgeReference> shared_parent;
        bool same_single_parent=true;
        for(const auto index:indices) {
            const int shape_index=edges.FindIndex(result.edges[index].shape);
            if(!shape_index) {
                same_single_parent=false;
                break;
            }
            const auto& metadata=*edge_metadata[
                static_cast<std::size_t>(shape_index)];
            if(metadata.references.size()!=1||
               (shared_parent&&
                *shared_parent!=metadata.references.front())) {
                same_single_parent=false;
                break;
            }
            shared_parent=metadata.references.front();
        }
        if(!same_single_parent||!shared_parent)continue;
        const auto alias=reference_key(*shared_parent);
        shared_edge_aliases.insert(alias);
        for(const auto index:indices) {
            result.edges[index].reference=*shared_parent;
            result.edges[index].history_index=reference_history(
                *shared_parent,result.edges[index].history_index);
        }
    }

    result.vertices.reserve(boundary.vertices.size());
    for(int vertex_index=1;vertex_index<=vertices.Extent();++vertex_index) {
        const auto& metadata=*vertex_metadata[
            static_cast<std::size_t>(vertex_index)];
        const bool may_inherit=metadata.references.size()==1&&
            vertex_parent_counts.at(
                reference_key(metadata.references.front()))==1;
        if(may_inherit) {
            const auto& reference=metadata.references.front();
            result.vertices.push_back({TopoDS::Vertex(vertices(vertex_index)),
                reference,reference_history(reference,metadata.history_index)});
            continue;
        }

        std::set<std::string> context,primary_candidates;
        std::size_t latest=metadata.history_index==
                std::numeric_limits<std::size_t>::max()?0:
            metadata.history_index;
        add_reference_context(context,latest,"source-vertex:",
            metadata.references,latest);
        for(const auto& reference:metadata.references)
            primary_candidates.insert(encoded_reference(reference));
        const int face_adjacency=vertex_faces.FindIndex(
            vertices(vertex_index));
        if(face_adjacency)
            for(TopTools_ListIteratorOfListOfShape adjacent(
                    vertex_faces.FindFromIndex(face_adjacency));
                    adjacent.More();adjacent.Next()) {
                const int face_index=faces.FindIndex(adjacent.Value());
                if(!face_index)continue;
                const auto& face=*named_faces[
                    static_cast<std::size_t>(face_index)];
                const auto encoded=encoded_reference(face.reference);
                context.insert("face:"+encoded);
                primary_candidates.insert(encoded);
                latest=std::max(latest,face.history_index);
            }
        const int edge_adjacency=vertex_edges.FindIndex(
            vertices(vertex_index));
        if(edge_adjacency)
            for(TopTools_ListIteratorOfListOfShape adjacent(
                    vertex_edges.FindFromIndex(edge_adjacency));
                    adjacent.More();adjacent.Next()) {
                const int edge_index=edges.FindIndex(adjacent.Value());
                if(!edge_index)continue;
                const auto& edge=*named_edges[
                    static_cast<std::size_t>(edge_index)];
                const auto encoded=encoded_reference(edge.reference);
                context.insert("edge:"+encoded);
                primary_candidates.insert(encoded);
                latest=std::max(latest,edge.history_index);
            }
        if(primary_candidates.empty())
            throw std::runtime_error(
                "Boundary topology vertex has no persisted ancestry");
        VertexReference reference{generated_owner(latest),
            direct_boundary_key(name_space,"vertex",
                *primary_candidates.begin(),context),{}};
        result.vertices.push_back({TopoDS::Vertex(vertices(vertex_index)),
            std::move(reference),latest});
    }

    const auto require_unique=[](std::string_view kind,const auto& entries,
            const std::set<std::string>& allowed_aliases) {
        std::map<std::string,std::size_t> identities;
        for(const auto& entry:entries) {
            const auto identity=reference_key(entry.reference);
            const auto count=++identities[identity];
            if(count>1&&!allowed_aliases.contains(identity)) {
                if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
                    std::cerr<<"[boundary identity collision] kind="
                        <<kind<<" count="<<count
                        <<" identity="<<identity<<'\n';
                throw std::runtime_error(
                    "Boundary topology produced an ambiguous "+
                    std::string(kind)+" identity");
            }
        }
    };
    const std::set<std::string> no_aliases;
    require_unique("face",result.faces,no_aliases);
    require_unique("edge",result.edges,shared_edge_aliases);
    require_unique("vertex",result.vertices,no_aliases);
    return result;
}

OcctBoundaryNamedTopology occt_boundary_named_topology(
        const OcctBoundaryUnion& boundary,
        std::span<const std::string> history_owners,double tolerance) {
    return occt_boundary_named_topology_impl(boundary,history_owners,
        tolerance,"boundary-union:add:");
}

OcctBoundaryNamedTopology occt_boundary_composition_named_topology(
        const OcctBoundaryUnion& boundary,
        std::span<const std::string> history_owners,double tolerance) {
    return occt_boundary_named_topology_impl(boundary,history_owners,
        tolerance,"boundary-composition:signed:");
}

std::vector<OcctResolvedBooleanEdge> occt_resolve_add_intersection_edges(
        const TopoDS_Shape& boundary,
        std::span<const OcctBoundaryFace> source_faces,
        std::span<const EdgeReference> requested,double tolerance) {
    if(boundary.IsNull())
        throw std::invalid_argument(
            "Boolean intersection resolution requires a calculated boundary");
    if(!std::isfinite(tolerance)||tolerance<=0.)
        throw std::invalid_argument(
            "Boolean intersection resolution tolerance must be finite and positive");

    const auto decode_set=[](std::string_view encoded) {
        std::set<std::string> result;
        std::size_t offset{};
        while(offset<encoded.size()) {
            const auto colon=encoded.find(':',offset);
            if(colon==std::string_view::npos)
                throw std::invalid_argument(
                    "Boolean intersection reference has an invalid ancestry set");
            std::size_t length{};
            const auto text=encoded.substr(offset,colon-offset);
            const auto parsed=std::from_chars(
                text.data(),text.data()+text.size(),length);
            if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||
               colon+1+length>encoded.size())
                throw std::invalid_argument(
                    "Boolean intersection reference has an invalid ancestry set");
            result.emplace(encoded.substr(colon+1,length));
            offset=colon+1+length;
        }
        return result;
    };
    std::map<std::string,std::vector<TopoDS_Face>> faces_by_reference;
    for(const auto& face:source_faces) {
        if(face.shape.IsNull())
            throw std::invalid_argument(
                "Boolean intersection source face is null");
        for(const auto& reference:face.references) {
            if(!reference.valid()||!reference.instance_path.empty())
                throw std::invalid_argument(
                    "Boolean intersection source requires local stable face identities");
            faces_by_reference[encoded_reference(reference)].push_back(
                face.shape);
        }
    }
    TopTools_IndexedMapOfShape boundary_edges;
    TopExp::MapShapes(boundary,TopAbs_EDGE,boundary_edges);
    TopTools_IndexedMapOfShape boundary_faces,boundary_vertices;
    TopExp::MapShapes(boundary,TopAbs_FACE,boundary_faces);
    TopExp::MapShapes(boundary,TopAbs_VERTEX,boundary_vertices);
    std::vector<std::set<std::string>> current_face_references(
        static_cast<std::size_t>(boundary_faces.Extent())+1);
    bool complete_current_ancestry=true;
    for(const auto& face:source_faces) {
        const int index=boundary_faces.FindIndex(face.shape);
        if(!index) {
            complete_current_ancestry=false;
            continue;
        }
        auto& references=current_face_references[
            static_cast<std::size_t>(index)];
        for(const auto& reference:face.references)
            references.insert(encoded_reference(reference));
    }
    for(int index=1;index<=boundary_faces.Extent();++index)
        complete_current_ancestry=complete_current_ancestry&&
            !current_face_references[static_cast<std::size_t>(index)].empty();

    std::vector<std::set<std::string>> current_edge_references,
        current_vertex_references;
    if(complete_current_ancestry) {
        TopTools_IndexedDataMapOfShapeListOfShape edge_faces,vertex_faces;
        TopExp::MapShapesAndAncestors(boundary,TopAbs_EDGE,TopAbs_FACE,
            edge_faces);
        TopExp::MapShapesAndAncestors(boundary,TopAbs_VERTEX,TopAbs_FACE,
            vertex_faces);
        const auto gather=[&](const auto& members,const auto& ancestors) {
            std::vector<std::set<std::string>> result(
                static_cast<std::size_t>(members.Extent())+1);
            for(int index=1;index<=members.Extent();++index) {
                const int ancestry=ancestors.FindIndex(members(index));
                if(!ancestry)continue;
                for(TopTools_ListIteratorOfListOfShape face(
                        ancestors.FindFromIndex(ancestry));face.More();
                        face.Next()) {
                    const int face_index=boundary_faces.FindIndex(
                        face.Value());
                    if(!face_index)continue;
                    const auto& references=current_face_references[
                        static_cast<std::size_t>(face_index)];
                    result[static_cast<std::size_t>(index)].insert(
                        references.begin(),references.end());
                }
            }
            return result;
        };
        current_edge_references=gather(boundary_edges,edge_faces);
        current_vertex_references=gather(boundary_vertices,vertex_faces);
    }
    const double incidence_tolerance=std::max(1.0e-7,2.*tolerance);
    const auto point_on_reference=[&](const gp_Pnt& point,
            const std::string& reference) {
        const auto found=faces_by_reference.find(reference);
        if(found==faces_by_reference.end())return false;
        const auto probe=BRepBuilderAPI_MakeVertex(point).Vertex();
        return std::ranges::any_of(found->second,[&](const TopoDS_Face& face) {
            BRepExtrema_DistShapeShape distance(probe,face);
            return distance.IsDone()&&distance.NbSolution()>0&&
                distance.Value()<=incidence_tolerance;
        });
    };
    const auto edge_on_reference=[&](const TopoDS_Edge& edge,
            const std::string& reference) {
        if(complete_current_ancestry) {
            const int index=boundary_edges.FindIndex(edge);
            return index&&current_edge_references[
                static_cast<std::size_t>(index)].contains(reference);
        }
        BRepAdaptor_Curve curve(edge);
        const double first=curve.FirstParameter();
        const double last=curve.LastParameter();
        if(!std::isfinite(first)||!std::isfinite(last)||last<first)
            return false;
        for(int sample=0;sample<9;++sample)
            if(!point_on_reference(curve.Value(
                    first+(last-first)*sample/8.),reference))
                return false;
        return true;
    };

    constexpr std::string_view prefix=
        "boolean:add:intersection:between:";
    std::vector<OcctResolvedBooleanEdge> result;
    result.reserve(requested.size());
    std::set<std::string> identities;
    for(const auto& reference:requested) {
        if(!reference.valid()||!reference.instance_path.empty()||
           !reference.semantic_key.starts_with(prefix))
            throw std::invalid_argument(
                "Boolean intersection resolver accepts local Add-intersection identities only");
        if(!identities.insert(reference_key(reference)).second)
            throw std::invalid_argument(
                "Boolean intersection resolver received a duplicate identity");
        const auto separator=reference.semantic_key.find(
            ":ends:",prefix.size());
        if(separator==std::string::npos)
            throw std::invalid_argument(
                "Boolean intersection reference has no endpoint ancestry");
        const auto parents=decode_set(std::string_view(reference.semantic_key).
            substr(prefix.size(),separator-prefix.size()));
        if(parents.size()<2||std::ranges::any_of(parents,[&](const auto& parent) {
                return !faces_by_reference.contains(parent);
            }))
            throw std::invalid_argument(
                "Boolean intersection reference has unavailable parent faces");
        const auto encoded_endpoints=decode_set(
            std::string_view(reference.semantic_key).substr(separator+6));
        std::vector<std::set<std::string>> endpoint_supports;
        endpoint_supports.reserve(encoded_endpoints.size());
        for(const auto& encoded_endpoint:encoded_endpoints) {
            auto support=decode_set(encoded_endpoint);
            if(std::ranges::any_of(support,[&](const auto& parent) {
                    return !faces_by_reference.contains(parent);
                }))
                throw std::invalid_argument(
                    "Boolean intersection reference has unavailable endpoint faces");
            endpoint_supports.push_back(std::move(support));
        }

        std::vector<TopoDS_Edge> matches;
        for(int index=1;index<=boundary_edges.Extent();++index) {
            const auto edge=TopoDS::Edge(boundary_edges(index));
            if(!std::ranges::all_of(parents,[&](const auto& parent) {
                    return edge_on_reference(edge,parent);
                }))continue;
            if(!endpoint_supports.empty()) {
                TopTools_IndexedMapOfShape endpoints;
                TopExp::MapShapes(edge,TopAbs_VERTEX,endpoints);
                const bool complete=std::ranges::all_of(endpoint_supports,
                    [&](const auto& support) {
                        return std::ranges::any_of(
                            std::views::iota(1,endpoints.Extent()+1),
                            [&](int endpoint) {
                                if(complete_current_ancestry) {
                                    const int index=boundary_vertices.FindIndex(
                                        endpoints(endpoint));
                                    if(!index)return false;
                                    const auto& references=
                                        current_vertex_references[
                                            static_cast<std::size_t>(index)];
                                    return std::ranges::all_of(support,
                                        [&](const auto& parent) {
                                            return references.contains(parent);
                                        });
                                }
                                const auto point=BRep_Tool::Pnt(
                                    TopoDS::Vertex(endpoints(endpoint)));
                                return std::ranges::all_of(support,
                                    [&](const auto& parent) {
                                        return point_on_reference(point,parent);
                                    });
                            });
                    });
                if(!complete)continue;
            }
            matches.push_back(edge);
        }
        if(matches.size()!=1)
            throw std::runtime_error(
                "Boolean intersection identity does not resolve to one current edge");
        result.push_back({matches.front(),reference});
    }
    return result;
}

std::vector<OcctResolvedBooleanEdge> occt_resolve_edge_treatment_edges(
        const TopoDS_Shape& boundary,
        std::span<const OcctBoundaryFace> source_faces,
        std::span<const EdgeReference> requested,double tolerance) {
    std::vector<EdgeReference> incidence_requests;
    incidence_requests.reserve(requested.size());
    for(const auto& reference:requested) {
        if(!reference.valid()||!reference.instance_path.empty())
            throw std::invalid_argument(
                "Edge treatment resolution requires local stable identities");
        const auto& key=reference.semantic_key;
        const bool supported=key.starts_with("fillet:edge:from:")||
            key.starts_with("chamfer:edge:from:");
        const auto between=key.find(":between:");
        const auto ends=between==std::string::npos?std::string::npos:
            key.find(":ends:",between+9);
        if(!supported||between==std::string::npos||
           ends==std::string::npos||ends<=between+9)
            throw std::invalid_argument(
                "Edge treatment identity has no support ancestry");
        incidence_requests.push_back({reference.owner_id,
            "boolean:add:intersection:between:"+
                key.substr(between+9,ends-(between+9))+key.substr(ends),
            reference.instance_path});
    }
    auto result=occt_resolve_add_intersection_edges(boundary,source_faces,
        incidence_requests,tolerance);
    if(result.size()!=requested.size())
        throw std::runtime_error(
            "Edge treatment ancestry resolution is incomplete");
    for(std::size_t index=0;index<result.size();++index)
        result[index].reference=requested[index];
    return result;
}

} // namespace zima::kernel
