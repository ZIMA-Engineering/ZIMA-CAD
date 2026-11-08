#include "profile_solid_fixture.hpp"
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/occt_boundary_intersection_graph.hpp>
#include <zima/kernel/occt_boundary_union.hpp>
#include <zima/kernel/occt_planar_edge_fillet.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/edge_treatment_route_plan.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/workspace/workspace.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <fstream>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepTools_ShapeSet.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <IntTools_Context.hxx>
#include <IntTools_FClass2d.hxx>
#include <IntTools_FaceFace.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <ShapeFix_Solid.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Lin.hxx>

namespace {

using Clock = std::chrono::steady_clock;

class EnvironmentVariableScope {
public:
    EnvironmentVariableScope(const char* name,const char* value):name_(name) {
        if(const char* previous=std::getenv(name_.c_str()))
            previous_=previous;
        set(name_.c_str(),value);
    }
    ~EnvironmentVariableScope() noexcept {
        try {set(name_.c_str(),previous_?previous_->c_str():nullptr);}
        catch(...) {}
    }
private:
    static void set(const char* name,const char* value) {
#ifdef _WIN32
        if(_putenv_s(name,value?value:"")!=0)
            throw std::runtime_error(
                "Could not set a benchmark environment variable");
#else
        if(value) {
            if(setenv(name,value,1)!=0)
                throw std::runtime_error(
                    "Could not set a benchmark environment variable");
        } else if(unsetenv(name)!=0)
            throw std::runtime_error(
                "Could not clear a benchmark environment variable");
#endif
    }
    std::string name_;
    std::optional<std::string> previous_;
};

template <typename Function>
double milliseconds(Function&& function, int repetitions = 3);

int step_benchmark(const std::filesystem::path& source) {
    if (!std::filesystem::is_regular_file(source)) {
        std::cerr << "STEP benchmark input does not exist: " << source << '\n';
        return EXIT_FAILURE;
    }

    auto document = zima::document::PartDocument::create_default();
    document.history.clear();
    document.history_order.clear();
    auto step = zima::document::PartDocument::create_imported_step_container(
        std::filesystem::absolute(source));
    step.name = "STEP benchmark";

    zima::kernel::OcctKernel kernel;
    zima::kernel::BodyResult imported;
    const auto import_ms = milliseconds([&] {
        auto results = kernel.import_step_components(
            {{std::filesystem::absolute(source).string(), {}, {}, {},
              step.id, {}}});
        if (results.size() != 1 || results.front().kernel_shape.empty()) {
            std::abort();
        }
        imported = std::move(results.front());
    }, 1);

    step.imported_step.frozen_brep =
        std::make_shared<const std::string>(imported.kernel_shape);
    step.imported_step.topology = imported.imported_step_topology;
    document.history.push_back(step);
    document.insert_history_entry(
        zima::document::PartHistoryKind::Feature, step.id);

    auto marker = zima::test::rectangular_feature(document,{100,80,50});
    marker.name = "Rollback marker";
    marker.suppressed = true;
    document.history.push_back(marker);
    document.insert_history_entry(
        zima::document::PartHistoryKind::Feature, marker.id);

    const auto document_operations = document.kernel_operations();
    auto boundaries = std::vector<zima::kernel::BodyResult>{imported, imported};
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        boundaries[index].source_fingerprint = zima::kernel::history_fingerprint(
            document_operations, index + 1);
    }
    zima::document::DocumentSession session(document, std::move(boundaries));
    const auto body_copy_ms = milliseconds([&] {
        auto copy = imported;
        if (copy.mesh.vertices.size() != imported.mesh.vertices.size()) std::abort();
    });
    const auto rollback_ms = milliseconds([&] {
        const auto boundary = session.rollback_boundary(marker.id);
        if (!boundary || !boundary->input_body) std::abort();
    });

    zima::kernel::Vec3 minimum{}, maximum{};
    if (!imported.mesh.vertices.empty()) {
        minimum = maximum = imported.mesh.vertices.front();
        for (const auto& point : imported.mesh.vertices) {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }
    }
    const zima::kernel::Vec3 ray_origin{
        (minimum.x + maximum.x) * 0.5,
        (minimum.y + maximum.y) * 0.5,
        minimum.z - std::max(1.0, maximum.z - minimum.z)};
    std::size_t candidate_count{};
    bool source_face_candidate{};
    const auto picking_ms = milliseconds([&] {
        const auto candidates = zima::viewer::ordered_viewer_candidates(
            imported.mesh, ray_origin, {0.0, 0.0, 1.0},
            std::max(1.0e-6, (maximum.x - minimum.x) * 1.0e-4));
        candidate_count = candidates.size();
        source_face_candidate = std::ranges::any_of(candidates,
            [&](const auto& candidate) {
                return candidate.kind == zima::viewer::CandidateKind::Face &&
                    candidate.geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference &&
                    candidate.owner_id == step.id &&
                    candidate.semantic_key.starts_with("step:face:#");
            });
    });

    auto operations = document_operations;
    operations.resize(1);
    const auto explicit_recalculate_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history(operations);
        if (result.size() != operations.size()) std::abort();
    }, 1);

    const auto temporary = std::filesystem::temp_directory_path() /
        "zima-step-performance-benchmark.prtz";
    const auto save_ms = milliseconds([&] {
        document.save(temporary, session.calculated_boundaries());
    }, 1);
    std::vector<zima::kernel::BodyResult> loaded_boundaries;
    std::size_t loaded_topology_count{};
    const auto load_ms = milliseconds([&] {
        loaded_boundaries.clear();
        const auto loaded = zima::document::PartDocument::load(
            temporary, &loaded_boundaries);
        if (loaded.history.empty() || loaded_boundaries.empty()) std::abort();
        loaded_topology_count = loaded.history.front().imported_step.topology.size();
        if (loaded_topology_count != imported.imported_step_topology.size()) {
            std::abort();
        }
    }, 1);
    const auto part_file_bytes = std::filesystem::file_size(temporary);
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);

    const auto& references = imported.mesh.original_references;
    std::size_t topology_faces{};
    std::size_t topology_edges{};
    std::size_t topology_vertices{};
    for (const auto& identity : imported.imported_step_topology) {
        using Kind = zima::kernel::StepRequest::TopologyIdentity::Kind;
        if (identity.semantic_key.find("step:") != 0 ||
            identity.semantic_key.find(":#") == std::string::npos) {
            std::cerr << "STEP topology does not use source entity identity\n";
            return EXIT_FAILURE;
        }
        switch (identity.kind) {
            case Kind::Face: ++topology_faces; break;
            case Kind::Edge: ++topology_edges; break;
            case Kind::Vertex: ++topology_vertices; break;
        }
    }
    if (topology_faces == 0 || references.triangle_references.empty()) {
        std::cerr << "STEP import produced no persistent source faces\n";
        return EXIT_FAILURE;
    }
    if (!source_face_candidate) {
        std::cerr << "Viewer did not offer an imported STEP source face\n";
        return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(3)
              << "ZIMA_STEP_PERF_V1 path=\"" << source.string() << "\"\n"
              << "input bytes=" << std::filesystem::file_size(source) << '\n'
              << "geometry vertices=" << imported.mesh.vertices.size()
              << " triangles=" << imported.mesh.triangles.size() / 3
              << " brep_bytes=" << imported.kernel_shape.size()
              << " reference_triangles=" << references.triangle_references.size()
              << " reference_edges=" << references.edges.size()
              << " topology_faces=" << topology_faces
              << " topology_edges=" << topology_edges
              << " topology_vertices=" << topology_vertices << '\n'
              << "step_import mean_ms=" << import_ms << '\n'
              << "body_copy mean_ms=" << body_copy_ms << '\n'
              << "properties_rollback mean_ms=" << rollback_ms << '\n'
              << "viewer_picking candidates=" << candidate_count
              << " source_face=1"
              << " mean_ms=" << picking_ms << '\n'
              << "explicit_recalculate mean_ms=" << explicit_recalculate_ms << '\n'
              << "part_save mean_ms=" << save_ms << '\n'
              << "part_load bytes=" << part_file_bytes
              << " topology=" << loaded_topology_count
              << " mean_ms=" << load_ms << '\n';
    return EXIT_SUCCESS;
}

template <typename Function>
double milliseconds(Function&& function, int repetitions) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    for (int index = 0; index < repetitions; ++index) {
        const auto start = Clock::now();
        function();
        const auto finish = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(
            finish - start).count());
    }
    double total = 0.0;
    for (const auto sample : samples) total += sample;
    return total / static_cast<double>(samples.size());
}

zima::document::PartDocument part_fixture(std::size_t feature_count) {
    auto part = zima::document::PartDocument::create_default();
    part.history.clear();
    part.history_order.clear();
    for (std::size_t index = 0; index < feature_count; ++index) {
        auto feature = zima::test::rectangular_feature(part,{20.0,20.0,20.0});
        feature.id = "benchmark-box-" + std::to_string(index);
        for (auto& profile : part.sketches)
            if (profile.id == feature.extrusion.sketch_id)
                profile.owner_container_id = feature.id;
        feature.feature_id = feature.id + ":feature";
        feature.feature_parent_id = feature.id;
        feature.container_origin = zima::document::create_container_origin(feature.id);
        feature.name = feature.id;
        feature.placement.x = static_cast<double>(index % 8) * 12.0;
        feature.placement.y = static_cast<double>(index / 8) * 12.0;
        feature.placement.z = 0.0;


        part.history.push_back(feature);
        part.insert_history_entry(zima::document::PartHistoryKind::Feature,
                                  feature.id);
    }
    part.resolve_constructions();
    return part;
}

zima::sketcher::Sketch solver_fixture(std::size_t branch_count) {
    auto sketch = zima::sketcher::Sketch::create_default();
    for (std::size_t index = 0; index < branch_count; ++index) {
        auto anchor = zima::sketcher::Sketch::create_point(
            static_cast<double>(index) * 3.0,
            static_cast<double>(index) * 2.0);
        anchor.fixed = true;
        auto moving = zima::sketcher::Sketch::create_point(
            anchor.x + 3.0, anchor.y + 0.75);
        const auto anchor_id = anchor.id;
        const auto moving_id = moving.id;
        sketch.points.push_back(std::move(anchor));
        sketch.points.push_back(std::move(moving));
        auto segment = zima::sketcher::Sketch::create_segment(
            anchor_id, moving_id);
        const auto segment_id = segment.id;
        sketch.segments.push_back(std::move(segment));
        sketch.constraints.push_back({
            "perf-h:" + std::to_string(index),
            zima::sketcher::ConstraintKind::Horizontal,
            anchor_id, moving_id, false, segment_id});
        sketch.dimensions.push_back({
            "perf-d:" + std::to_string(index),
            zima::sketcher::DimensionKind::Distance,
            anchor_id, moving_id, 5.0});
    }
    return sketch;
}

zima::sketcher::Sketch connected_solver_fixture(std::size_t segment_count) {
    auto sketch = zima::sketcher::Sketch::create_default();
    auto first = zima::sketcher::Sketch::create_point(0.0, 0.0);
    first.fixed = true;
    sketch.points.push_back(std::move(first));
    for (std::size_t index = 0; index < segment_count; ++index) {
        auto point = zima::sketcher::Sketch::create_point(
            static_cast<double>(index + 1) * 3.0,
            index % 2 == 0 ? 0.5 : -0.5);
        const auto first_id = sketch.points.back().id;
        const auto second_id = point.id;
        sketch.points.push_back(std::move(point));
        auto segment = zima::sketcher::Sketch::create_segment(first_id, second_id);
        const auto segment_id = segment.id;
        sketch.segments.push_back(std::move(segment));
        sketch.constraints.push_back({
            "chain-h:" + std::to_string(index),
            zima::sketcher::ConstraintKind::Horizontal,
            first_id, second_id, false, segment_id});
        sketch.dimensions.push_back({
            "chain-d:" + std::to_string(index),
            zima::sketcher::DimensionKind::Distance,
            first_id, second_id, 3.0});
    }
    return sketch;
}

std::size_t mobility_component_count(const zima::sketcher::Sketch& sketch) {
    std::unordered_map<std::string, std::vector<std::string>> neighbors;
    for (const auto& point : sketch.points) neighbors[point.id];
    const auto connect = [&](const std::string& first, const std::string& second) {
        if (first.empty() || second.empty() || first == second ||
            !neighbors.contains(first) || !neighbors.contains(second)) return;
        neighbors[first].push_back(second);
        neighbors[second].push_back(first);
    };
    for (const auto& segment : sketch.segments) {
        connect(segment.first_point_id, segment.second_point_id);
    }
    for (const auto& constraint : sketch.constraints) {
        connect(constraint.first_point_id, constraint.second_point_id);
    }
    for (const auto& dimension : sketch.dimensions) {
        connect(dimension.first_point_id, dimension.second_point_id);
    }
    std::unordered_set<std::string> visited;
    std::size_t components{};
    for (const auto& [root, _] : neighbors) {
        if (visited.contains(root)) continue;
        ++components;
        std::vector<std::string> pending{root};
        while (!pending.empty()) {
            auto point = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(point).second) continue;
            for (const auto& neighbor : neighbors.at(point)) {
                if (!visited.contains(neighbor)) pending.push_back(neighbor);
            }
        }
    }
    return components;
}

}  // namespace

int batch_add_probe(const std::filesystem::path& source,
        std::size_t first,std::size_t last,bool measure_sequential=true,
        bool verify_equivalence=true,
        const std::optional<std::filesystem::path>& identity_template={},
        bool spatial_only=false,bool treatment_only=false,
        bool localized_treatment_only=false) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    const auto document=document::PartDocument::load(source,&previous);
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto operations=document.kernel_operations(false,true);
    const auto body_operation_index=[&](std::size_t index) {
        return static_cast<std::size_t>(std::ranges::count_if(
            operations.begin(),operations.begin()+
                static_cast<std::ptrdiff_t>(index+1),
            [&](const auto& operation) {
                return operation.body.id==operations[index].body.id;
            })-1);
    };
    if(first==0||first>last||last>=operations.size()||last>=previous.size())
        throw std::runtime_error("Batch Add range is outside the calculated history");
    if(identity_template) {
        kernel::OcctKernel current_kernel;
        previous=current_kernel.evaluate_history(operations);
    }
    const auto boolean_operation=operations[first].operation;
    const auto body_id=operations[first].body.id;
    for(std::size_t index=first;index<=last;++index)
        if(operations[index].suppressed||
           operations[index].operation!=boolean_operation||
           operations[index].body.id!=body_id)
            throw std::runtime_error("Batch range mixes Boolean operations or contains a suppressed operation");
    const auto body_at=[&](const std::vector<kernel::BodyResult>& values,
            std::size_t boundary)->const kernel::BodyResult& {
        const auto& value=values.at(boundary);
        const auto found=value.body_boundaries.find(body_id);
        const auto& local=found==value.body_boundaries.end()
            ?value:found->second.back();
        const auto output=local.body_outputs.find(body_id);
        return output==local.body_outputs.end()?local:output->second.get();
    };
    const auto local_body=[&](std::size_t boundary)->const kernel::BodyResult& {
        return body_at(previous,boundary);
    };
    std::vector<kernel::BodyResult> identity_boundaries;
    if(identity_template) {
        static_cast<void>(document::PartDocument::load(
            *identity_template,&identity_boundaries));
        if(identity_boundaries.size()<=last)
            throw std::runtime_error(
                "Batch identity template has insufficient history");
    }
    const auto identity_body=[&](std::size_t boundary)
            ->const kernel::BodyResult& {
        return identity_template?body_at(identity_boundaries,boundary)
            :local_body(boundary);
    };
    double sequential_ms=-1.;
    if(measure_sequential) {
        const auto sequential_start=Clock::now();
        kernel::OcctKernel sequential_kernel;
        const std::vector<kernel::HistoryOperation> sequential_operations(
            operations.begin(),operations.begin()+static_cast<std::ptrdiff_t>(last+1));
        const std::vector<kernel::BodyResult> sequential_prefix(
            previous.begin(),previous.begin()+static_cast<std::ptrdiff_t>(first));
        const auto sequential=sequential_kernel.evaluate_history_incremental(
            sequential_operations,sequential_prefix);
        sequential_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-sequential_start).count();
        if(sequential.size()!=last+1)
            throw std::runtime_error("Sequential comparison did not produce every boundary");
    }
    const auto reference_key=[](const auto& reference) {
        return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+
            reference.instance_path;
    };
    const auto encoded_reference=[](const auto& reference) {
        return std::to_string(reference.owner_id.size())+":"+
            reference.owner_id+std::to_string(reference.semantic_key.size())+
            ":"+reference.semantic_key+
            std::to_string(reference.instance_path.size())+":"+
            reference.instance_path;
    };
    const auto encoded_reference_set=[](const std::set<std::string>& references) {
        std::string encoded;
        for(const auto& reference:references)
            encoded+=std::to_string(reference.size())+":"+reference;
        return encoded;
    };
    struct BoundFaceShape {
        TopoDS_Shape root;
        std::vector<std::pair<TopoDS_Shape,kernel::FaceReference>> faces;
        std::vector<std::pair<TopoDS_Shape,kernel::EdgeReference>> edges;
        std::vector<std::pair<TopoDS_Shape,kernel::VertexReference>> vertices;
    };
    const auto bound_face_shape=[&](const kernel::BodyResult& body) {
        BoundFaceShape result;
        if(body.kernel_shape.empty()||
           body.kernel_shape==kernel::kernel_body_compound||
           !body.kernel_placements.empty())return result;
        BRep_Builder builder;
        BRepTools_ShapeSet archive(builder);
        std::istringstream source(body.kernel_shape.get());
        archive.Read(source);
        archive.Read(result.root,source);
        if(result.root.IsNull())return result;
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(result.root,TopAbs_FACE,members);
        for(const auto& binding:body.kernel_faces) {
            TopoDS_Shape face;
            std::istringstream locator(binding.locator);
            archive.Read(face,locator);
            if(!face.IsNull()&&face.ShapeType()==TopAbs_FACE&&members.Contains(face))
                result.faces.emplace_back(std::move(face),
                    binding.reference);
        }
        members.Clear();TopExp::MapShapes(result.root,TopAbs_EDGE,members);
        for(const auto& binding:body.kernel_edges) {
            TopoDS_Shape edge;
            std::istringstream locator(binding.locator);
            archive.Read(edge,locator);
            if(!edge.IsNull()&&edge.ShapeType()==TopAbs_EDGE&&members.Contains(edge))
                result.edges.emplace_back(std::move(edge),
                    binding.reference);
        }
        members.Clear();TopExp::MapShapes(result.root,TopAbs_VERTEX,members);
        for(const auto& binding:body.kernel_vertices) {
            TopoDS_Shape vertex;
            std::istringstream locator(binding.locator);
            archive.Read(vertex,locator);
            if(!vertex.IsNull()&&vertex.ShapeType()==TopAbs_VERTEX&&
                    members.Contains(vertex))
                result.vertices.emplace_back(std::move(vertex),
                    binding.reference);
        }
        return result;
    };
    std::unordered_map<std::string,std::size_t> operation_positions;
    for(std::size_t index=0;index<operations.size();++index)
        operation_positions.emplace(operations[index].owner_id,index);
    const auto graph_faces=[&](const BoundFaceShape& shape,
            std::size_t fallback_history_index) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(shape.root,TopAbs_FACE,members);
        std::vector<kernel::OcctBoundaryFace> result(
            static_cast<std::size_t>(members.Extent()));
        for(int index=1;index<=members.Extent();++index) {
            auto& face=result[static_cast<std::size_t>(index-1)];
            face.shape=TopoDS::Face(members(index));
            face.history_index=fallback_history_index;
        }
        for(const auto& [member,reference]:shape.faces) {
            const int index=members.FindIndex(member);
            if(!index)continue;
            auto& face=result[static_cast<std::size_t>(index-1)];
            face.references.push_back(reference);
            if(const auto position=operation_positions.find(
                    reference.owner_id);position!=operation_positions.end())
                face.history_index=std::min(face.history_index,
                    position->second);
        }
        for(auto& face:result) {
            if(face.references.empty())
                throw std::runtime_error(
                    "Boundary graph probe face has no stable reference");
            std::ranges::sort(face.references,[&](const auto& left,
                    const auto& right) {
                const auto age=[&](const auto& reference) {
                    const auto found=operation_positions.find(
                        reference.owner_id);
                    return found==operation_positions.end()
                        ?fallback_history_index:found->second;
                };
                return std::tuple{age(left),left.owner_id,
                    left.semantic_key,left.instance_path}<
                    std::tuple{age(right),right.owner_id,
                    right.semantic_key,right.instance_path};
            });
            const auto duplicate=std::ranges::unique(face.references,
                [](const auto& left,const auto& right) {
                    return left.owner_id==right.owner_id&&
                        left.semantic_key==right.semantic_key&&
                        left.instance_path==right.instance_path;
                });
            face.references.erase(duplicate.begin(),
                face.references.end());
        }
        return result;
    };
    const auto graph_edges=[&](const BoundFaceShape& shape,
            std::size_t fallback_history_index) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(shape.root,TopAbs_EDGE,members);
        std::vector<kernel::OcctBoundarySolid::Edge> result;
        std::vector<int> outputs(
            static_cast<std::size_t>(members.Extent())+1,-1);
        for(const auto& [member,reference]:shape.edges) {
            const int index=members.FindIndex(member);
            if(!index)continue;
            auto& output=outputs[static_cast<std::size_t>(index)];
            if(output<0) {
                output=static_cast<int>(result.size());
                result.push_back({TopoDS::Edge(members(index)),{},
                    fallback_history_index});
            }
            auto& edge=result[static_cast<std::size_t>(output)];
            edge.references.push_back(reference);
            if(const auto position=operation_positions.find(
                    reference.owner_id);position!=operation_positions.end())
                edge.history_index=std::min(edge.history_index,
                    position->second);
        }
        for(auto& edge:result) {
            std::ranges::sort(edge.references,[&](const auto& left,
                    const auto& right) {
                const auto age=[&](const auto& reference) {
                    const auto found=operation_positions.find(
                        reference.owner_id);
                    return found==operation_positions.end()
                        ?fallback_history_index:found->second;
                };
                return std::tuple{age(left),reference_key(left)}<
                    std::tuple{age(right),reference_key(right)};
            });
            const auto duplicate=std::ranges::unique(edge.references);
            edge.references.erase(duplicate.begin(),duplicate.end());
        }
        return result;
    };
    const auto graph_vertices=[&](const BoundFaceShape& shape,
            std::size_t fallback_history_index) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(shape.root,TopAbs_VERTEX,members);
        std::vector<kernel::OcctBoundarySolid::Vertex> result;
        std::vector<int> outputs(
            static_cast<std::size_t>(members.Extent())+1,-1);
        for(const auto& [member,reference]:shape.vertices) {
            const int index=members.FindIndex(member);
            if(!index)continue;
            auto& output=outputs[static_cast<std::size_t>(index)];
            if(output<0) {
                output=static_cast<int>(result.size());
                result.push_back({TopoDS::Vertex(members(index)),{},
                    fallback_history_index});
            }
            auto& vertex=result[static_cast<std::size_t>(output)];
            vertex.references.push_back(reference);
            if(const auto position=operation_positions.find(
                    reference.owner_id);position!=operation_positions.end())
                vertex.history_index=std::min(vertex.history_index,
                    position->second);
        }
        for(auto& vertex:result) {
            std::ranges::sort(vertex.references,[&](const auto& left,
                    const auto& right) {
                const auto age=[&](const auto& reference) {
                    const auto found=operation_positions.find(
                        reference.owner_id);
                    return found==operation_positions.end()
                        ?fallback_history_index:found->second;
                };
                return std::tuple{age(left),reference_key(left)}<
                    std::tuple{age(right),reference_key(right)};
            });
            const auto duplicate=std::ranges::unique(vertex.references);
            vertex.references.erase(duplicate.begin(),duplicate.end());
        }
        return result;
    };
    auto input_shape=bound_face_shape(local_body(first-1));
    if(input_shape.root.IsNull()&&!identity_template) {
        kernel::OcctKernel current_kernel;
        previous=current_kernel.evaluate_history(operations);
        input_shape=bound_face_shape(local_body(first-1));
    }
    if(input_shape.root.IsNull()&&identity_template)
        input_shape=bound_face_shape(identity_body(first-1));
    if(input_shape.root.IsNull()) {
        std::cerr<<"active body="<<body_id<<" local kernel="
            <<local_body(first-1).kernel_shape.get()
            <<" outputs="<<local_body(first-1).body_outputs.size();
        for(const auto& [id,output]:local_body(first-1).body_outputs)
            std::cerr<<" output="<<id<<':'<<output->kernel_shape.get();
        std::cerr<<'\n';
        throw std::runtime_error("Batch Add input has no bound native shape");
    }
    auto reusable_graph_faces=graph_faces(input_shape,first-1);
    auto reusable_graph_edges=graph_edges(input_shape,first-1);
    auto reusable_graph_vertices=graph_vertices(input_shape,first-1);
    std::vector<std::vector<kernel::OcctBoundaryFace>>
        reusable_graph_tool_faces;
    std::vector<std::vector<kernel::OcctBoundarySolid::Edge>>
        reusable_graph_tool_edges;
    std::vector<std::vector<kernel::OcctBoundarySolid::Vertex>>
        reusable_graph_tool_vertices;
    auto source_faces=std::move(input_shape.faces);
    auto source_edges=std::move(input_shape.edges);
    auto source_vertices=std::move(input_shape.vertices);
    kernel::OcctKernel kernel;
    std::vector<TopoDS_Shape> tools;
    std::map<std::string,kernel::BodyResult> feature_tools;
    std::map<std::size_t,kernel::BodyResult> minimal_reference_sets;
    std::set<std::size_t> through_all_tools;
    const auto target_references=[](const kernel::HistoryOperation& operation) {
        std::vector<kernel::FaceReference> result;
        const auto append=[&](const kernel::FaceReference& reference) {
            if(reference.valid()&&std::ranges::none_of(result,[&](const auto& current) {
                    return current.owner_id==reference.owner_id&&
                        current.semantic_key==reference.semantic_key&&
                        current.instance_path==reference.instance_path;
                }))result.push_back(reference);
        };
        const auto extrusion=[&](const kernel::ExtrusionRequest& request) {
            if((request.extent==kernel::ExtrusionRequest::Extent::UpToPlane||
                request.extent==kernel::ExtrusionRequest::Extent::UpToSurface)&&
               !request.target_is_datum)append(request.target_face);
            if(request.reverse_limit&&!request.reverse_limit->datum)
                append(request.reverse_limit->reference);
            if(request.ordered_end_limit)for(const auto& patch:
                    request.ordered_end_limit->patches)
                if(!patch.target.datum)append(patch.target.reference);
        };
        const auto revolution=[&](const kernel::RevolutionRequest& request) {
            if(request.end_limit&&!request.end_limit->datum)
                append(request.end_limit->reference);
            if(request.ordered_end_limit)for(const auto& patch:
                    request.ordered_end_limit->patches)
                if(!patch.target.datum)append(patch.target.reference);
        };
        if(const auto* group=std::get_if<kernel::FeatureGroupRequest>(
                &operation.primitive))for(const auto& child:group->children)
            std::visit([&](const auto& request) {
                using Request=std::decay_t<decltype(request)>;
                if constexpr(std::is_same_v<Request,kernel::ExtrusionRequest>)
                    extrusion(request);
                else if constexpr(std::is_same_v<Request,kernel::RevolutionRequest>)
                    revolution(request);
            },child);
        else if(const auto* request=std::get_if<kernel::ExtrusionRequest>(
                    &operation.primitive))extrusion(*request);
        else if(const auto* request=std::get_if<kernel::RevolutionRequest>(
                    &operation.primitive))revolution(*request);
        return result;
    };
    double minimal_reference_capture_ms{},minimal_tool_ms{};
    std::size_t minimal_reference_count{},minimal_reference_bytes{};
    std::set<std::string> unique_minimal_references;
    const auto tool_start=Clock::now();
    for(std::size_t index=first;index<=last;++index) {
        std::cerr<<"batch tool index="<<index<<" owner="
            <<operations[index].owner_id<<" body="
            <<operations[index].body.id<<" local="
            <<body_operation_index(index)<<'\n';
        const auto& references=previous.at(index-1);
        kernel::BodyResult tool;
        if(operations[index].feature_copy) {
            const auto& request=*operations[index].feature_copy;
            const auto source_tool=feature_tools.find(request.source_feature_id);
            if(source_tool==feature_tools.end())
                throw std::runtime_error("Batch Add copy source is outside its independent block");
            tool=request.combination==kernel::BodyCombination::Pattern
                ? kernel.pattern_body(source_tool->second,request.pattern,
                    operations[index].owner_id)
                : kernel.mirror_body(source_tool->second,request.mirror_plane,
                    operations[index].owner_id);
        } else if(std::holds_alternative<kernel::FeatureGroupRequest>(
                operations[index].primitive)) {
            const auto full_tool=kernel.calculate_feature_tool(
                operations[index],references,{},references,{});
            const auto capture_start=Clock::now();
            kernel::BodyResult minimal_references;
            const auto targets=target_references(operations[index]);
            for(const auto& target:targets)
                std::cerr<<"batch target index="<<index
                    <<" owner="<<target.owner_id<<'\n';
            if(!targets.empty()) {
                minimal_references.kernel_shape=kernel::kernel_body_compound;
                const std::vector<kernel::BodyResult> prefix(
                    previous.begin(),previous.begin()+index);
                for(std::size_t target=0;target<targets.size();++target) {
                    auto captured=kernel.capture_reference_face_history(
                        prefix,targets[target]);
                    minimal_reference_bytes+=captured.kernel_shape.size();
                    unique_minimal_references.insert(
                        reference_key(targets[target]));
                    const auto id="target:"+std::to_string(target);
                    minimal_references.kernel_compound_bodies.push_back(id);
                    minimal_references.body_outputs.emplace(id,std::move(captured));
                }
                minimal_references.source_fingerprint="minimal-reference-set";
            }
            minimal_reference_count+=targets.size();
            minimal_reference_capture_ms+=std::chrono::duration<double,std::milli>(
                Clock::now()-capture_start).count();
            const auto group=std::get<kernel::FeatureGroupRequest>(
                operations[index].primitive);
            const bool through_all=std::ranges::any_of(group.children,
                [](const auto& child) {
                    const auto* request=std::get_if<kernel::ExtrusionRequest>(&child);
                    return request&&(request->extent==
                        kernel::ExtrusionRequest::Extent::ThroughAll||
                        request->through_all_reverse);
                });
            kernel::BodyResult direct_references;
            if(!targets.empty()) {
                direct_references.kernel_shape=kernel::kernel_body_compound;
                const std::vector<kernel::BodyResult> prefix(
                    previous.begin(),previous.begin()+
                        static_cast<std::ptrdiff_t>(first));
                for(std::size_t target=0;target<targets.size();++target) {
                    const auto direct_source=feature_tools.find(
                        targets[target].owner_id);
                    auto captured=direct_source==feature_tools.end()
                        ?kernel.capture_reference_face_history(
                            prefix,targets[target])
                        :kernel.capture_reference_face(
                            direct_source->second,targets[target]);
                    const auto id="target:"+std::to_string(target);
                    direct_references.kernel_compound_bodies.push_back(id);
                    direct_references.body_outputs.emplace(
                        id,std::move(captured));
                }
                direct_references.source_fingerprint="direct-reference-set";
            }
            kernel::BodyResult direct_input;
            if(through_all) {
                direct_input.kernel_shape=kernel::kernel_body_compound;
                direct_input.kernel_compound_bodies.push_back("base");
                direct_input.body_outputs.emplace(
                    "base",local_body(first-1));
                for(const auto& [owner,value]:feature_tools) {
                    direct_input.kernel_compound_bodies.push_back(owner);
                    direct_input.body_outputs.emplace(owner,value);
                }
                direct_input.source_fingerprint="direct-input-set";
            }
            const auto minimal_start=Clock::now();
            auto minimal_tool=kernel.calculate_feature_tool(operations[index],
                minimal_references,{},through_all?references:kernel::BodyResult{},{});
            minimal_tool_ms+=std::chrono::duration<double,std::milli>(
                Clock::now()-minimal_start).count();
            if(minimal_tool.kernel_shape.get()!=full_tool.kernel_shape.get()||
               minimal_tool.kernel_faces.size()!=full_tool.kernel_faces.size()||
               minimal_tool.kernel_edges.size()!=full_tool.kernel_edges.size()||
               minimal_tool.kernel_vertices.size()!=full_tool.kernel_vertices.size())
                throw std::runtime_error(
                    "Minimal Feature dependencies changed its exact tool");
            auto direct_tool=kernel.calculate_feature_tool(
                operations[index],direct_references,{},direct_input,{});
            if(direct_tool.kernel_shape.get()!=full_tool.kernel_shape.get()||
               direct_tool.kernel_faces.size()!=full_tool.kernel_faces.size()||
               direct_tool.kernel_edges.size()!=full_tool.kernel_edges.size()||
               direct_tool.kernel_vertices.size()!=full_tool.kernel_vertices.size()) {
                const auto full_shape=kernel::read_calculated_body_shape(
                    full_tool);
                const auto direct_shape=kernel::read_calculated_body_shape(
                    direct_tool);
                const auto difference=[&](const TopoDS_Shape& left,
                        const TopoDS_Shape& right) {
                    BRepAlgoAPI_Cut cut(left,right);
                    cut.SetFuzzyValue(std::max(1.0e-7,
                        operations[index].boolean_tolerance));
                    cut.Build();
                    if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
                    GProp_GProps properties;
                    BRepGProp::VolumeProperties(cut.Shape(),properties);
                    return std::abs(properties.Mass());
                };
                std::cerr<<"batch direct tool difference index="<<index
                    <<" full_minus_direct="
                    <<difference(full_shape,direct_shape)
                    <<" direct_minus_full="
                    <<difference(direct_shape,full_shape)
                    <<" full_faces="<<full_tool.kernel_faces.size()
                    <<" direct_faces="<<direct_tool.kernel_faces.size()
                    <<" full_edges="<<full_tool.kernel_edges.size()
                    <<" direct_edges="<<direct_tool.kernel_edges.size()
                    <<" full_vertices="<<full_tool.kernel_vertices.size()
                    <<" direct_vertices="<<direct_tool.kernel_vertices.size()
                    <<'\n';
            }
            minimal_reference_sets.emplace(index,minimal_references);
            if(through_all)through_all_tools.insert(index);
            tool=std::move(minimal_tool);
        } else if(std::holds_alternative<kernel::ExtrusionRequest>(
                      operations[index].primitive)||
                  std::holds_alternative<kernel::RevolutionRequest>(
                      operations[index].primitive)||
                  std::holds_alternative<kernel::Sweep3DRequest>(
                      operations[index].primitive)||
                  std::holds_alternative<kernel::LoftRequest>(
                      operations[index].primitive)) {
            auto standalone=operations[index];
            standalone.body={};standalone.operation=kernel::BooleanOperation::Add;
            standalone.suppressed=false;standalone.feature_copy.reset();
            auto result=kernel.evaluate_history({standalone});
            if(result.size()!=1)
                throw std::runtime_error("Standalone Add tool has no result");
            tool=std::move(result.front());
        } else throw std::runtime_error(
            "Batch Add range contains a stateful operation barrier");
        feature_tools.insert_or_assign(operations[index].owner_id,tool);
        if(tool.kernel_shape.empty())continue;
        auto shape=bound_face_shape(tool);
        if(!shape.root.IsNull()) {
            reusable_graph_tool_faces.push_back(graph_faces(shape,index));
            reusable_graph_tool_edges.push_back(graph_edges(shape,index));
            reusable_graph_tool_vertices.push_back(
                graph_vertices(shape,index));
            tools.push_back(std::move(shape.root));
            source_faces.insert(source_faces.end(),
                std::make_move_iterator(shape.faces.begin()),
                std::make_move_iterator(shape.faces.end()));
            source_edges.insert(source_edges.end(),
                std::make_move_iterator(shape.edges.begin()),
                std::make_move_iterator(shape.edges.end()));
            source_vertices.insert(source_vertices.end(),
                std::make_move_iterator(shape.vertices.begin()),
                std::make_move_iterator(shape.vertices.end()));
        }
    }
    const auto parallel_tool_start=Clock::now();
    std::vector<std::pair<std::size_t,std::future<kernel::BodyResult>>> pending_tools;
    for(std::size_t index=first;index<=last;++index) {
        if(operations[index].feature_copy)continue;
        pending_tools.emplace_back(index,std::async(std::launch::async,[&,index] {
            kernel::OcctKernel task_kernel;
            if(!std::holds_alternative<kernel::FeatureGroupRequest>(
                    operations[index].primitive)) {
                auto standalone=operations[index];
                standalone.body={};standalone.operation=kernel::BooleanOperation::Add;
                standalone.suppressed=false;standalone.feature_copy.reset();
                auto result=task_kernel.evaluate_history({standalone});
                if(result.size()!=1)throw std::runtime_error(
                    "Parallel standalone tool has no result");
                return std::move(result.front());
            }
            const auto& references=minimal_reference_sets.at(index);
            return task_kernel.calculate_feature_tool(operations[index],references,{},
                through_all_tools.contains(index)?previous.at(index-1):kernel::BodyResult{},{});
        }));
    }
    std::map<std::string,kernel::BodyResult> parallel_feature_tools;
    for(auto& [index,future]:pending_tools)
        parallel_feature_tools.emplace(operations[index].owner_id,future.get());
    kernel::OcctKernel copy_kernel;
    for(std::size_t index=first;index<=last;++index)if(operations[index].feature_copy) {
        const auto& request=*operations[index].feature_copy;
        const auto source_tool=parallel_feature_tools.find(request.source_feature_id);
        if(source_tool==parallel_feature_tools.end())
            throw std::runtime_error("Parallel Batch copy source is unavailable");
        auto tool=request.combination==kernel::BodyCombination::Pattern
            ?copy_kernel.pattern_body(source_tool->second,request.pattern,
                operations[index].owner_id)
            :copy_kernel.mirror_body(source_tool->second,request.mirror_plane,
                operations[index].owner_id);
        parallel_feature_tools.emplace(operations[index].owner_id,std::move(tool));
    }
    const auto parallel_tool_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-parallel_tool_start).count();
    const auto hot_tool_start=Clock::now();
    if(operations[first].feature_copy||
       !std::holds_alternative<kernel::FeatureGroupRequest>(
           operations[first].primitive))
        throw std::runtime_error("Hot Batch probe needs a directly editable Feature");
    kernel::OcctKernel hot_tool_kernel;
    const auto hot_tool=hot_tool_kernel.calculate_feature_tool(
        operations[first],minimal_reference_sets.at(first),{},
        through_all_tools.contains(first)?previous.at(first-1):kernel::BodyResult{},{});
    if(hot_tool.kernel_shape.empty())
        throw std::runtime_error("Hot Batch probe did not rebuild the changed tool");
    const auto hot_tool_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-hot_tool_start).count();
    const auto tool_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-tool_start).count();
    if(tools.empty())throw std::runtime_error("Batch Add range produced no solid tools");
    double tolerance=1.0e-7;
    for(std::size_t index=first;index<=last;++index)
        tolerance=std::max(tolerance,operations[index].boolean_tolerance);

    if(std::getenv("ZIMA_CPP_BATCH_FUSE_ONLY")) {
        TopTools_ListOfShape arguments,tool_list;
        arguments.Append(input_shape.root);
        for(const auto& tool:tools)tool_list.Append(tool);
        BRepAlgoAPI_BooleanOperation fuse;
        fuse.SetOperation(boolean_operation==kernel::BooleanOperation::Add
            ?BOPAlgo_FUSE:BOPAlgo_CUT);
        fuse.SetArguments(arguments);
        fuse.SetTools(tool_list);
        fuse.SetRunParallel(true);
        fuse.SetToFillHistory(true);
        fuse.SetFuzzyValue(tolerance);
        const auto fuse_start=Clock::now();
        fuse.Build();
        const auto fuse_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-fuse_start).count();
        const bool raw_available=fuse.IsDone()&&!fuse.Shape().IsNull();
        const bool raw_valid=raw_available&&
            BRepCheck_Analyzer(fuse.Shape(),true,true,false).IsValid();
        TopoDS_Shape unified;
        double unify_ms{};
        bool unified_valid{};
        TopoDS_Shape protected_shape;
        double protected_unify_ms{};
        bool protected_valid{};
        std::size_t protected_edges{};
        if(raw_available) {
            const auto unify_start=Clock::now();
            ShapeUpgrade_UnifySameDomain unifier(
                fuse.Shape(),true,true,false);
            unifier.SetLinearTolerance(tolerance);
            unifier.SetAngularTolerance(1.0e-9);
            unifier.Build();
            unified=unifier.Shape();
            unify_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-unify_start).count();
            unified_valid=!unified.IsNull()&&
                BRepCheck_Analyzer(unified,true,true,false).IsValid();

            TopTools_IndexedMapOfShape raw_faces;
            TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,raw_faces);
            std::vector<std::vector<kernel::FaceReference>> face_parents(
                static_cast<std::size_t>(raw_faces.Extent())+1);
            const auto attach_parent=[&](const TopoDS_Shape& face,
                    const kernel::FaceReference& reference) {
                const int index=raw_faces.FindIndex(face);
                if(index&&std::ranges::find(face_parents[
                        static_cast<std::size_t>(index)],reference)==
                        face_parents[static_cast<std::size_t>(index)].end())
                    face_parents[static_cast<std::size_t>(index)].push_back(
                        reference);
            };
            for(const auto& [source,reference]:source_faces) {
                bool changed=false;
                for(const auto* descendants:{&fuse.Modified(source),
                        &fuse.Generated(source)})
                    for(TopTools_ListIteratorOfListOfShape descendant(
                            *descendants);descendant.More();descendant.Next())
                        if(descendant.Value().ShapeType()==TopAbs_FACE) {
                            attach_parent(descendant.Value(),reference);
                            changed=true;
                        }
                if(!changed&&!fuse.IsDeleted(source))
                    attach_parent(source,reference);
            }
            TopTools_IndexedMapOfShape probe_edges;
            TopExp::MapShapes(unified,TopAbs_EDGE,probe_edges);
            const auto edge_survives=[&](const TopoDS_Shape& edge) {
                if(probe_edges.Contains(edge))return true;
                const auto history=unifier.History();
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
            ShapeUpgrade_UnifySameDomain protected_unifier(
                fuse.Shape(),true,true,false);
            protected_unifier.SetLinearTolerance(tolerance);
            protected_unifier.SetAngularTolerance(1.0e-9);
            TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
            TopExp::MapShapesAndAncestors(fuse.Shape(),TopAbs_EDGE,
                TopAbs_FACE,edge_faces);
            for(int edge=1;edge<=edge_faces.Extent();++edge) {
                const auto& adjacent=edge_faces.FindFromIndex(edge);
                if(adjacent.Extent()!=2)continue;
                TopTools_ListIteratorOfListOfShape iterator(adjacent);
                const auto first_face=TopoDS::Face(iterator.Value());
                const int first_index=raw_faces.FindIndex(first_face);
                iterator.Next();
                const auto second_face=TopoDS::Face(iterator.Value());
                const int second_index=raw_faces.FindIndex(second_face);
                bool may_merge=first_index&&second_index;
                if(may_merge) {
                    const auto& first=face_parents[
                        static_cast<std::size_t>(first_index)];
                    const auto& second=face_parents[
                        static_cast<std::size_t>(second_index)];
                    may_merge=std::ranges::any_of(first,[&](const auto& left) {
                        return std::ranges::find(second,left)!=second.end();
                    });
                    if(!may_merge) {
                        may_merge=std::ranges::any_of(first,[&](const auto& left) {
                            return std::ranges::any_of(second,[&](const auto& right) {
                                return left.surface_result==right.surface_result&&
                                    left.sheet_role==right.sheet_role;
                            });
                        });
                        if(may_merge) {
                            const BRepAdaptor_Surface first_surface(first_face),
                                second_surface(second_face);
                            may_merge=first_surface.GetType()==GeomAbs_Plane&&
                                second_surface.GetType()==GeomAbs_Plane;
                            if(may_merge) {
                                auto first_normal=first_surface.Plane().Axis().Direction();
                                auto second_normal=second_surface.Plane().Axis().Direction();
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
                        }
                    }
                }
                if(!may_merge&&!edge_survives(edge_faces.FindKey(edge))) {
                    protected_unifier.KeepShape(edge_faces.FindKey(edge));
                    ++protected_edges;
                }
            }
            const auto protected_start=Clock::now();
            protected_unifier.Build();
            protected_shape=protected_unifier.Shape();
            protected_unify_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-protected_start).count();
            protected_valid=!protected_shape.IsNull()&&
                BRepCheck_Analyzer(
                    protected_shape,true,true,false).IsValid();
        }
        const auto topology=[](const TopoDS_Shape& shape) {
            std::array<int,4> result{};
            if(shape.IsNull())return result;
            for(std::size_t index=0;index<result.size();++index) {
                TopTools_IndexedMapOfShape members;
                TopExp::MapShapes(shape,std::array{TopAbs_SOLID,
                    TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}[index],members);
                result[index]=members.Extent();
            }
            return result;
        };
        const auto expected=kernel::read_calculated_body_shape(
            local_body(last));
        const auto expected_topology=topology(expected);
        const auto raw_topology=topology(
            raw_available?fuse.Shape():TopoDS_Shape{});
        const auto unified_topology=topology(unified);
        const auto protected_topology=topology(protected_shape);
        std::cout<<std::setprecision(17)
            <<"batch_fuse_only first="<<first<<" last="<<last
            <<" tools="<<tools.size()
            <<" fuse_ms="<<fuse_ms
            <<" unify_ms="<<unify_ms
            <<" protected_unify_ms="<<protected_unify_ms
            <<" raw_available="<<raw_available
            <<" raw_valid="<<raw_valid
            <<" unified_valid="<<unified_valid
            <<" protected_valid="<<protected_valid
            <<" protected_edges="<<protected_edges
            <<" expected_topology="<<expected_topology[0]<<','
                <<expected_topology[1]<<','<<expected_topology[2]<<','
                <<expected_topology[3]
            <<" raw_topology="<<raw_topology[0]<<','
                <<raw_topology[1]<<','<<raw_topology[2]<<','
                <<raw_topology[3]
            <<" unified_topology="<<unified_topology[0]<<','
                <<unified_topology[1]<<','<<unified_topology[2]<<','
                <<unified_topology[3]
            <<" protected_topology="<<protected_topology[0]<<','
                <<protected_topology[1]<<','<<protected_topology[2]<<','
                <<protected_topology[3]<<'\n';
        return raw_valid&&unified_valid&&protected_valid?0:2;
    }

    // Measure the proposed lightweight history representation separately
    // from Fuse.  Keep the already-authored and newly added solids as a
    // compound, and calculate only the exact trim curves introduced by each
    // Add.  PCurves are requested because a useful boundary graph must be able
    // to trim both supporting faces; timing a bare display-only 3D section
    // would understate the production work.
    TopoDS_Compound section_compound;
    BRep_Builder section_builder;
    section_builder.MakeCompound(section_compound);
    section_builder.Add(section_compound,input_shape.root);
    std::size_t section_edges{};
    std::vector<TopoDS_Edge> section_edge_shapes;
    const auto section_start=Clock::now();
    for(const auto& tool:tools) {
        BRepAlgoAPI_Section section(section_compound,tool,false);
        section.SetNonDestructive(true);section.SetRunParallel(true);
        section.SetFuzzyValue(tolerance);section.Approximation(true);
        section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);
        section.Build();
        if(!section.IsDone()||section.Shape().IsNull())
            throw std::runtime_error("Batch Add section probe failed");
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
        section_edges+=static_cast<std::size_t>(edges.Extent());
        for(int edge=1;edge<=edges.Extent();++edge)
            section_edge_shapes.push_back(TopoDS::Edge(edges(edge)));
        section_builder.Add(section_compound,tool);
    }
    const auto section_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-section_start).count();

    // The boundary graph must never ask the Boolean engine to compare every
    // face with every other face.  Measure the narrower operation that the
    // proposed graph needs: broad-phase AABB filtering followed by exact
    // face/face intersections only for overlapping candidates.  The source
    // set grows after each tool so this covers tool/base and tool/earlier-tool
    // contacts without constructing intermediate result solids.
    struct SpatialFace {
        TopoDS_Face face;
        Bnd_Box bounds;
    };
    const auto spatial_faces=[](const TopoDS_Shape& shape) {
        std::vector<SpatialFace> result;
        for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
            SpatialFace value{TopoDS::Face(face.Current()),{}};
            // The ordinary box is conservative and uses the face's existing
            // triangulation when present.  A broad phase needs no optimal box;
            // false positives are cheap while recomputing a mathematically
            // tight box for every curved face is measurable work.
            BRepBndLib::Add(value.face,value.bounds,true);
            result.push_back(std::move(value));
        }
        return result;
    };
    const auto spatial_bounds_start=Clock::now();
    auto accumulated_faces=spatial_faces(input_shape.root);
    std::vector<std::vector<SpatialFace>> tool_faces;
    tool_faces.reserve(tools.size());
    for(const auto& tool:tools)tool_faces.push_back(spatial_faces(tool));
    const auto spatial_bounds_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-spatial_bounds_start).count();
    std::size_t spatial_possible_pairs{};
    std::vector<std::pair<TopoDS_Face,TopoDS_Face>> spatial_candidates;
    for(std::size_t tool_index=0;tool_index<tools.size();++tool_index) {
        auto& current_faces=tool_faces[tool_index];
        spatial_possible_pairs+=accumulated_faces.size()*current_faces.size();
        for(const auto& source:accumulated_faces)
            for(const auto& current:current_faces)
                if(!source.bounds.IsOut(current.bounds))
                    spatial_candidates.emplace_back(
                        source.face,current.face);
        accumulated_faces.insert(accumulated_faces.end(),
            current_faces.begin(),current_faces.end());
    }
    std::cerr<<"spatial face pairs possible="<<spatial_possible_pairs
        <<" candidates="<<spatial_candidates.size()
        <<" bounds_ms="<<spatial_bounds_ms<<'\n';
    std::size_t spatial_nonempty_pairs{},spatial_section_curves{};
    std::size_t spatial_section_points{},spatial_tangent_pairs{};
    std::size_t spatial_curves_with_both_pcurves{};
    TopTools_IndexedMapOfShape spatial_face_map;
    TopExp::MapShapes(input_shape.root,TopAbs_FACE,spatial_face_map);
    for(const auto& tool:tools)
        TopExp::MapShapes(tool,TopAbs_FACE,spatial_face_map);
    std::vector<std::size_t> spatial_face_reference_counts(
        static_cast<std::size_t>(spatial_face_map.Extent())+1);
    for(const auto& [face,reference]:source_faces) {
        static_cast<void>(reference);
        const int index=spatial_face_map.FindIndex(face);
        if(index)++spatial_face_reference_counts[
            static_cast<std::size_t>(index)];
    }
    const auto spatial_faces_without_reference=std::ranges::count(
        spatial_face_reference_counts,0)-1;
    using CurveSignature=std::array<long long,9>;
    const auto point_curve_signature=[](const gp_Pnt& first_point,
            const gp_Pnt& last_point,const gp_Pnt& middle_point) {
        const auto point=[](const gp_Pnt& value) {
            constexpr double quantum=1.0e-6;
            return std::array{
                std::llround(value.X()/quantum),
                std::llround(value.Y()/quantum),
                std::llround(value.Z()/quantum)};
        };
        auto begin=point(first_point),end=point(last_point);
        if(end<begin)std::swap(begin,end);
        const auto middle=point(middle_point);
        return CurveSignature{begin[0],begin[1],begin[2],
            end[0],end[1],end[2],
            middle[0],middle[1],middle[2]};
    };
    std::set<CurveSignature> spatial_curve_signatures;
    std::set<CurveSignature> spatial_parented_curve_signatures;
    std::size_t spatial_bounded_curves{},spatial_parented_curves{};
    const auto curve_segment_signature=[&](const IntTools_Curve& curve,
            double first,double last)->std::optional<CurveSignature> {
        gp_Pnt first_point,last_point,middle_point;
        if(!curve.D0(first,first_point)||!curve.D0(last,last_point)||
           !curve.D0((first+last)/2.,middle_point))return {};
        return point_curve_signature(first_point,last_point,middle_point);
    };
    const auto curve_signature=[&](const IntTools_Curve& curve)
            ->std::optional<CurveSignature> {
        double first{},last{};gp_Pnt first_point,last_point;
        if(!curve.Bounds(first,last,first_point,last_point))return {};
        return curve_segment_signature(curve,first,last);
    };
    struct SpatialIntersectionCurve {
        IntTools_Curve curve;
        TopoDS_Face first_face;
        TopoDS_Face second_face;
        bool stable_parents{};
    };
    std::vector<SpatialIntersectionCurve> spatial_intersection_curves;
    std::vector<std::pair<TopoDS_Face,TopoDS_Face>>
        spatial_tangent_face_pairs;
    std::map<CurveSignature,std::size_t> section_signature_counts;
    struct CurveInterval {
        occ::handle<Geom_Curve> curve;
        double first{};
        double last{};
        Bnd_Box bounds;
    };
    std::vector<CurveInterval> section_curve_intervals;
    std::size_t section_edges_without_signature{};
    for(const auto& edge:section_edge_shapes) {
        double first{},last{};
        const auto curve=BRep_Tool::Curve(edge,first,last);
        gp_Pnt begin,end,middle;
        if(curve.IsNull()||!std::isfinite(first)||!std::isfinite(last)||
           last<=first) {
            ++section_edges_without_signature;
            continue;
        }
        curve->D0(first,begin);curve->D0(last,end);
        curve->D0((first+last)/2.,middle);
        ++section_signature_counts[
            point_curve_signature(begin,end,middle)];
        CurveInterval interval{curve,first,last,{}};
        GeomAdaptor_Curve adaptor(curve,first,last);
        BndLib_Add3dCurve::Add(adaptor,std::max(1.0e-7,tolerance),
            interval.bounds);
        section_curve_intervals.push_back(std::move(interval));
    }
    occ::handle<IntTools_Context> intersection_context=new IntTools_Context;
    const auto spatial_section_start=Clock::now();
    for(std::size_t candidate=0;candidate<spatial_candidates.size();
            ++candidate) {
        const auto& [source,current]=spatial_candidates[candidate];
        IntTools_FaceFace intersection;
        intersection.SetContext(intersection_context);
        intersection.SetFuzzyValue(tolerance);
        intersection.SetParameters(true,true,true,tolerance);
        intersection.Perform(source,current,false);
        if(!intersection.IsDone())
            throw std::runtime_error(
                "Spatial low-level face-pair intersection probe failed");
        const auto& lines=intersection.Lines();
        const auto& points=intersection.Points();
        if(!lines.IsEmpty()||!points.IsEmpty())++spatial_nonempty_pairs;
        spatial_section_curves+=static_cast<std::size_t>(lines.Size());
        spatial_section_points+=static_cast<std::size_t>(points.Size());
        if(intersection.TangentFaces()) {
            ++spatial_tangent_pairs;
            spatial_tangent_face_pairs.emplace_back(source,current);
        }
        const int source_index=spatial_face_map.FindIndex(source);
        const int current_index=spatial_face_map.FindIndex(current);
        const bool stable_parents=source_index&&current_index&&
            spatial_face_reference_counts[
                static_cast<std::size_t>(source_index)]&&
            spatial_face_reference_counts[
                static_cast<std::size_t>(current_index)];
        for(int line=1;line<=lines.Size();++line) {
            const auto& curve=lines.Value(line);
            if(!curve.Curve().IsNull()&&
               !curve.FirstCurve2d().IsNull()&&
               !curve.SecondCurve2d().IsNull())
                ++spatial_curves_with_both_pcurves;
            if(stable_parents)++spatial_parented_curves;
            spatial_intersection_curves.push_back(
                {curve,source,current,stable_parents});
            if(const auto signature=curve_signature(curve)) {
                ++spatial_bounded_curves;
                spatial_curve_signatures.insert(*signature);
                if(stable_parents)
                    spatial_parented_curve_signatures.insert(*signature);
            }
        }
        if((candidate+1)%64==0)
            std::cerr<<"spatial face pairs processed="<<(candidate+1)
                <<" elapsed_ms="
                <<std::chrono::duration<double,std::milli>(
                    Clock::now()-spatial_section_start).count()<<'\n';
    }
    const auto spatial_section_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-spatial_section_start).count();

    // Establish the exact contribution of tangent/coplanar face pairs.  This
    // is a reference measurement only: the production graph will intersect
    // their 2D trimming wires directly instead of constructing Section
    // topology per pair.
    std::vector<CurveInterval> tangent_section_curve_intervals;
    std::size_t tangent_section_edges{};
    const auto tangent_section_start=Clock::now();
    for(const auto& [source,current]:spatial_tangent_face_pairs) {
        BRepAlgoAPI_Section section(source,current,false);
        section.SetNonDestructive(true);section.SetRunParallel(false);
        section.SetFuzzyValue(tolerance);section.Approximation(true);
        section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);
        section.Build();
        if(!section.IsDone())
            throw std::runtime_error("Tangent face Section probe failed");
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
        tangent_section_edges+=static_cast<std::size_t>(edges.Extent());
        for(int index=1;index<=edges.Extent();++index) {
            const auto edge=TopoDS::Edge(edges(index));
            double first{},last{};
            const auto curve=BRep_Tool::Curve(edge,first,last);
            if(curve.IsNull()||last<=first)continue;
            CurveInterval interval{curve,first,last,{}};
            GeomAdaptor_Curve adaptor(curve,first,last);
            BndLib_Add3dCurve::Add(adaptor,
                std::max(1.0e-7,tolerance),interval.bounds);
            tangent_section_curve_intervals.push_back(
                std::move(interval));
        }
    }
    const auto tangent_section_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-tangent_section_start).count();

    // IntTools_FaceFace intersects the underlying bounded surfaces.  Split
    // each resulting PCurve at the wires of both parent faces and retain only
    // parameter intervals classified inside both trimmed domains.  This is the
    // exact topological work required by the proposed boundary graph, without
    // creating a Section compound or a Boolean result solid.
    const auto trim_parameters=[&](const TopoDS_Face& face,
            const occ::handle<Geom2d_Curve>& pcurve,
            double first,double last) {
        std::vector<double> parameters{first,last};
        if(pcurve.IsNull()||last<=first)return parameters;
        occ::handle<Geom2d_Curve> trimmed=
            new Geom2d_TrimmedCurve(pcurve,first,last);
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
            double edge_first{},edge_last{};
            auto edge_curve=BRep_Tool::CurveOnSurface(
                TopoDS::Edge(edge.Current()),face,edge_first,edge_last);
            if(edge_curve.IsNull()||edge_last<=edge_first)continue;
            occ::handle<Geom2d_Curve> edge_trimmed=
                new Geom2d_TrimmedCurve(edge_curve,edge_first,edge_last);
            Geom2dAPI_InterCurveCurve intersection(
                trimmed,edge_trimmed,std::max(1.0e-7,tolerance));
            const auto& result=intersection.Intersector();
            if(!result.IsDone())continue;
            const auto append=[&](double parameter) {
                if(parameter>=first-1.0e-9&&parameter<=last+1.0e-9)
                    parameters.push_back(std::clamp(parameter,first,last));
            };
            for(int point=1;point<=result.NbPoints();++point)
                append(result.Point(point).ParamOnFirst());
            for(int segment=1;segment<=result.NbSegments();++segment) {
                const auto& overlap=result.Segment(segment);
                if(overlap.HasFirstPoint())
                    append(overlap.FirstPoint().ParamOnFirst());
                if(overlap.HasLastPoint())
                    append(overlap.LastPoint().ParamOnFirst());
            }
        }
        std::ranges::sort(parameters);
        const auto removed=std::ranges::unique(parameters,[](double left,
                double right) {
            return std::abs(left-right)<=1.0e-9*
                std::max({1.,std::abs(left),std::abs(right)});
        });
        parameters.erase(removed.begin(),parameters.end());
        return parameters;
    };
    std::size_t spatial_trimmed_intervals{};
    std::size_t spatial_parented_trimmed_intervals{};
    std::set<CurveSignature> spatial_trimmed_signatures;
    std::set<CurveSignature> spatial_parented_trimmed_signatures;
    std::map<CurveSignature,std::size_t> spatial_trimmed_signature_counts;
    std::vector<CurveInterval> spatial_trimmed_curve_intervals;
    const auto spatial_trim_start=Clock::now();
    for(const auto& entry:spatial_intersection_curves) {
        double first{},last{};gp_Pnt first_point,last_point;
        if(!entry.curve.Bounds(first,last,first_point,last_point)||
           last<=first)continue;
        auto parameters=trim_parameters(entry.first_face,
            entry.curve.FirstCurve2d(),first,last);
        const auto second=trim_parameters(entry.second_face,
            entry.curve.SecondCurve2d(),first,last);
        parameters.insert(parameters.end(),second.begin(),second.end());
        std::ranges::sort(parameters);
        const auto removed=std::ranges::unique(parameters,[](double left,
                double right) {
            return std::abs(left-right)<=1.0e-9*
                std::max({1.,std::abs(left),std::abs(right)});
        });
        parameters.erase(removed.begin(),parameters.end());
        auto& first_classifier=intersection_context->FClass2d(
            entry.first_face);
        auto& second_classifier=intersection_context->FClass2d(
            entry.second_face);
        for(std::size_t interval=1;interval<parameters.size();++interval) {
            const double begin=parameters[interval-1];
            const double end=parameters[interval];
            if(end<=begin)continue;
            const double middle=(begin+end)/2.;
            const auto first_state=first_classifier.Perform(
                entry.curve.FirstCurve2d()->Value(middle));
            const auto second_state=second_classifier.Perform(
                entry.curve.SecondCurve2d()->Value(middle));
            if(first_state==TopAbs_OUT||second_state==TopAbs_OUT)continue;
            ++spatial_trimmed_intervals;
            if(entry.stable_parents)
                ++spatial_parented_trimmed_intervals;
            if(const auto signature=curve_segment_signature(
                    entry.curve,begin,end)) {
                spatial_trimmed_signatures.insert(*signature);
                ++spatial_trimmed_signature_counts[*signature];
                if(entry.stable_parents)
                    spatial_parented_trimmed_signatures.insert(*signature);
            }
            CurveInterval curve_interval{
                entry.curve.Curve(),begin,end,{}};
            GeomAdaptor_Curve adaptor(
                curve_interval.curve,begin,end);
            BndLib_Add3dCurve::Add(adaptor,
                std::max(1.0e-7,tolerance),curve_interval.bounds);
            spatial_trimmed_curve_intervals.push_back(
                std::move(curve_interval));
        }
    }
    const auto spatial_trim_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-spatial_trim_start).count();
    std::size_t section_signatures_missing_from_trim{};
    std::size_t trimmed_signatures_missing_from_section{};
    std::size_t section_multiplicity_excess{},trimmed_multiplicity_excess{};
    for(const auto& [signature,count]:section_signature_counts) {
        const auto found=spatial_trimmed_signature_counts.find(signature);
        if(found==spatial_trimmed_signature_counts.end())
            ++section_signatures_missing_from_trim;
        else if(count>found->second)
            section_multiplicity_excess+=count-found->second;
    }
    for(const auto& [signature,count]:spatial_trimmed_signature_counts) {
        const auto found=section_signature_counts.find(signature);
        if(found==section_signature_counts.end())
            ++trimmed_signatures_missing_from_section;
        else if(count>found->second)
            trimmed_multiplicity_excess+=count-found->second;
    }
    const auto coverage_start=Clock::now();
    const auto curve_is_covered=[&](const CurveInterval& source,
            const std::vector<CurveInterval>& candidates) {
        constexpr int samples=5;
        for(const auto& candidate:candidates) {
            if(source.bounds.IsOut(candidate.bounds))continue;
            GeomAPI_ProjectPointOnCurve projection;
            projection.Init(candidate.curve,candidate.first,candidate.last);
            bool covered=true;
            for(int sample=0;sample<samples;++sample) {
                gp_Pnt point;
                const double parameter=source.first+
                    (source.last-source.first)*sample/(samples-1.);
                source.curve->D0(parameter,point);
                projection.Perform(point);
                if(!projection.NbPoints()||projection.LowerDistance()>
                        std::max(1.0e-6,2.*tolerance)) {
                    covered=false;break;
                }
            }
            if(covered)return true;
        }
        return false;
    };
    const auto curve_is_covered_by_union=[&](const CurveInterval& source,
            const std::vector<CurveInterval>& candidates) {
        constexpr int samples=9;
        for(int sample=0;sample<samples;++sample) {
            gp_Pnt point;
            const double parameter=source.first+
                (source.last-source.first)*sample/(samples-1.);
            source.curve->D0(parameter,point);
            bool covered=false;
            for(const auto& candidate:candidates) {
                if(candidate.bounds.IsOut(point))continue;
                GeomAPI_ProjectPointOnCurve projection(
                    point,candidate.curve,candidate.first,candidate.last);
                if(projection.NbPoints()&&projection.LowerDistance()<=
                        std::max(1.0e-6,2.*tolerance)) {
                    covered=true;break;
                }
            }
            if(!covered)return false;
        }
        return true;
    };
    const auto section_edges_covered_by_trim=std::ranges::count_if(
        section_curve_intervals,[&](const auto& interval) {
            return curve_is_covered_by_union(interval,
                spatial_trimmed_curve_intervals);
        });
    auto graph_curve_intervals=spatial_trimmed_curve_intervals;
    graph_curve_intervals.insert(graph_curve_intervals.end(),
        tangent_section_curve_intervals.begin(),
        tangent_section_curve_intervals.end());
    const auto section_edges_covered_by_graph=std::ranges::count_if(
        section_curve_intervals,[&](const auto& interval) {
            return curve_is_covered_by_union(interval,
                graph_curve_intervals);
        });
    const auto reusable_graph_start=Clock::now();
    std::vector<CurveInterval> reusable_graph_curve_intervals;
    std::size_t reusable_graph_curves{};
    std::size_t reusable_graph_transverse{};
    std::size_t reusable_graph_coplanar{};
    std::vector<kernel::OcctBoundaryCurve> reusable_graph_source_curves;
    for(const auto& tool:reusable_graph_tool_faces) {
        const auto graph=kernel::occt_boundary_intersection_graph(
            reusable_graph_faces,tool,tolerance);
        reusable_graph_curves+=graph.curves.size();
        reusable_graph_transverse+=
            graph.metrics.retained_transverse_intervals;
        reusable_graph_coplanar+=
            graph.metrics.retained_coplanar_intervals;
        for(const auto& curve:graph.curves) {
            reusable_graph_source_curves.push_back(curve);
            CurveInterval interval{curve.curve,curve.first_parameter,
                curve.last_parameter,{}};
            GeomAdaptor_Curve adaptor(curve.curve,
                curve.first_parameter,curve.last_parameter);
            BndLib_Add3dCurve::Add(adaptor,
                std::max(1.0e-7,tolerance),interval.bounds);
            reusable_graph_curve_intervals.push_back(
                std::move(interval));
        }
        reusable_graph_faces.insert(reusable_graph_faces.end(),
            tool.begin(),tool.end());
    }
    const auto reusable_graph_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-reusable_graph_start).count();
    const auto section_edges_covered_by_reusable_graph=
        std::ranges::count_if(section_curve_intervals,
            [&](const auto& interval) {
                return curve_is_covered_by_union(interval,
                    reusable_graph_curve_intervals);
            });
    const auto reusable_graph_curves_covered_by_section=
        std::ranges::count_if(reusable_graph_curve_intervals,
            [&](const auto& interval) {
                return curve_is_covered_by_union(interval,
                    section_curve_intervals);
            });
    if(section_edges_covered_by_reusable_graph!=
            static_cast<std::ptrdiff_t>(section_curve_intervals.size())||
       reusable_graph_curves_covered_by_section!=
            static_cast<std::ptrdiff_t>(
                reusable_graph_curve_intervals.size()))
        throw std::runtime_error(
            "Reusable boundary graph changed Skeleton intersection geometry");
    const std::size_t tool_face_count=std::accumulate(
        reusable_graph_tool_faces.begin(),
        reusable_graph_tool_faces.end(),std::size_t{},
        [](std::size_t count,const auto& faces) {
            return count+faces.size();
        });
    if(tool_face_count>reusable_graph_faces.size()||
       reusable_graph_tool_faces.size()!=tools.size()||
       reusable_graph_tool_edges.size()!=tools.size()||
       reusable_graph_tool_vertices.size()!=tools.size())
        throw std::runtime_error(
            "Reusable boundary union probe has inconsistent inputs");
    const std::size_t base_face_count=
        reusable_graph_faces.size()-tool_face_count;
    std::vector<kernel::OcctBoundarySolid> reusable_union_inputs;
    reusable_union_inputs.push_back({input_shape.root,
        std::vector<kernel::OcctBoundaryFace>(
            reusable_graph_faces.begin(),
            reusable_graph_faces.begin()+
                static_cast<std::ptrdiff_t>(base_face_count)),
        reusable_graph_edges,reusable_graph_vertices});
    for(std::size_t tool=0;tool<tools.size();++tool) {
        TopTools_IndexedMapOfShape member_solids;
        TopExp::MapShapes(tools[tool],TopAbs_SOLID,member_solids);
        std::cerr<<"boundary union input="<<(tool+1)
            <<" solids="<<member_solids.Extent()
            <<" faces="<<reusable_graph_tool_faces[tool].size()<<'\n';
        reusable_union_inputs.push_back(
            {tools[tool],reusable_graph_tool_faces[tool],
                reusable_graph_tool_edges[tool],
                reusable_graph_tool_vertices[tool]});
    }
    const auto reusable_union_start=Clock::now();
    const auto reusable_union=kernel::occt_boundary_union(
        reusable_union_inputs,tolerance);
    const auto reusable_union_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-reusable_union_start).count();
    std::vector<std::string> history_owners;
    history_owners.reserve(operations.size());
    for(const auto& operation:operations)
        history_owners.push_back(operation.owner_id);
    const auto naming_start=Clock::now();
    const auto named_topology=kernel::occt_boundary_named_topology(
        reusable_union,history_owners,tolerance);
    const auto naming_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-naming_start).count();
    const auto repeated_naming=kernel::occt_boundary_named_topology(
        reusable_union,history_owners,tolerance);
    const auto named_identity_set=[&](const auto& entries) {
        std::set<std::string> result;
        for(const auto& entry:entries)
            result.insert(reference_key(entry.reference));
        return result;
    };
    if(named_identity_set(named_topology.faces)!=
            named_identity_set(repeated_naming.faces)||
       named_identity_set(named_topology.edges)!=
            named_identity_set(repeated_naming.edges)||
       named_identity_set(named_topology.vertices)!=
            named_identity_set(repeated_naming.vertices))
        throw std::runtime_error(
            "Direct boundary identities are not deterministic");
    const auto repeated_union_start=Clock::now();
    const auto repeated_union=kernel::occt_boundary_union(
        reusable_union_inputs,tolerance);
    const auto repeated_union_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-repeated_union_start).count();
    const auto repeated_named_topology=kernel::occt_boundary_named_topology(
        repeated_union,history_owners,tolerance);
    if(named_identity_set(named_topology.faces)!=
            named_identity_set(repeated_named_topology.faces)||
       named_identity_set(named_topology.edges)!=
            named_identity_set(repeated_named_topology.edges)||
       named_identity_set(named_topology.vertices)!=
            named_identity_set(repeated_named_topology.vertices))
        throw std::runtime_error(
            "Direct boundary identities changed after a fresh calculation");
    std::optional<kernel::BodyResult> published_block;
    std::optional<kernel::OcctKernel> published_kernel;
    double published_block_ms{},published_serialize_ms{};
    std::size_t published_json_bytes{};
    if(spatial_only) {
        std::vector<kernel::BodyResult> ordered_tools;
        std::vector<std::size_t> ordered_tool_indices;
        ordered_tools.reserve(last-first+1);
        ordered_tool_indices.reserve(last-first+1);
        for(std::size_t index=first;index<=last;++index) {
            const auto found=parallel_feature_tools.find(
                operations[index].owner_id);
            if(found==parallel_feature_tools.end())
                throw std::runtime_error(
                    "Published Add block lost a prepared tool");
            ordered_tools.push_back(found->second);
            ordered_tool_indices.push_back(index);
        }
        published_kernel.emplace();
        const auto& published_local=previous.back().body_boundaries.
                contains(body_id)
            ?previous.back().body_boundaries.at(body_id).get()
            :previous;
        std::vector<std::string> published_fingerprints;
        published_fingerprints.reserve(published_local.size());
        for(const auto& boundary:published_local)
            published_fingerprints.push_back(
                boundary.source_fingerprint);
        const auto publish_start=Clock::now();
        published_block=published_kernel->calculate_add_block(
            local_body(first-1),ordered_tools,ordered_tool_indices,
            history_owners,tolerance,operations[last].mesh_deflection,
            published_local.at(last).source_fingerprint,
            published_fingerprints);
        // The document packet owns the accumulated accepted automatic Up To
        // choices. The direct block is a replacement for that exact prefix,
        // so its continuation packet must expose the same accepted choices.
        published_block->resolved_up_to_limits=
            previous.back().resolved_up_to_limits;
        published_block_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-publish_start).count();
        const auto published_reference_set=[&](const auto& bindings) {
            std::set<std::string> result;
            for(const auto& binding:bindings)
                result.insert(reference_key(binding.reference));
            return result;
        };
        if(published_reference_set(published_block->kernel_faces)!=
                named_identity_set(named_topology.faces)||
           published_reference_set(published_block->kernel_edges)!=
                named_identity_set(named_topology.edges)||
           published_reference_set(published_block->kernel_vertices)!=
                named_identity_set(named_topology.vertices))
            throw std::runtime_error(
                "Published Add block changed its direct identities");
        const auto serialize_start=Clock::now();
        const auto packet=document::serialize_body_result(*published_block,
            false,false);
        const auto serialized=packet.dump();
        published_serialize_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-serialize_start).count();
        published_json_bytes=serialized.size();
        const auto restored=document::load_body_result(packet);
        if(published_reference_set(restored.kernel_faces)!=
                named_identity_set(named_topology.faces)||
           published_reference_set(restored.kernel_edges)!=
                named_identity_set(named_topology.edges)||
           published_reference_set(restored.kernel_vertices)!=
                named_identity_set(named_topology.vertices))
            throw std::runtime_error(
                "Published Add block identities changed during round-trip");
    }
    const auto identity_digest=[&](const auto& entries) {
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto& value:named_identity_set(entries)) {
            for(const unsigned char byte:value) {
                hash^=byte;
                hash*=1099511628211ULL;
            }
            hash^=0xffU;
            hash*=1099511628211ULL;
        }
        std::ostringstream encoded;
        encoded<<std::hex<<hash;
        return encoded.str();
    };
    TopTools_IndexedMapOfShape reusable_union_faces,reusable_union_edges,
        reusable_union_vertices;
    TopExp::MapShapes(reusable_union.shape,TopAbs_FACE,
        reusable_union_faces);
    TopExp::MapShapes(reusable_union.shape,TopAbs_EDGE,
        reusable_union_edges);
    TopExp::MapShapes(reusable_union.shape,TopAbs_VERTEX,
        reusable_union_vertices);
    GProp_GProps reusable_union_surface,reusable_union_volume;
    BRepGProp::SurfaceProperties(reusable_union.shape,
        reusable_union_surface);
    BRepGProp::VolumeProperties(reusable_union.shape,
        reusable_union_volume);
    if(!BRepCheck_Analyzer(reusable_union.shape).IsValid()||
       reusable_union_faces.Extent()!=
            static_cast<int>(reusable_union.faces.size())||
       reusable_union_edges.Extent()!=
            static_cast<int>(reusable_union.edges.size())||
       reusable_union_vertices.Extent()!=
            static_cast<int>(reusable_union.vertices.size()))
        throw std::runtime_error(
            "Reusable boundary union changed Skeleton validity or provenance");
    if(spatial_only) {
        const auto expected_shape=
            kernel::read_calculated_body_shape(local_body(last));
        const auto expected_bound=bound_face_shape(local_body(last));
        const auto reference_sets=[&] {
            struct Sets {
                std::set<std::string> faces,edges,vertices;
            } expected,parents;
            for(const auto& [shape,reference]:expected_bound.faces) {
                static_cast<void>(shape);
                expected.faces.insert(reference_key(reference));
            }
            for(const auto& [shape,reference]:expected_bound.edges) {
                static_cast<void>(shape);
                expected.edges.insert(reference_key(reference));
            }
            for(const auto& [shape,reference]:expected_bound.vertices) {
                static_cast<void>(shape);
                expected.vertices.insert(reference_key(reference));
            }
            for(const auto& face:reusable_union.faces)
                for(const auto& reference:face.references)
                    parents.faces.insert(reference_key(reference));
            for(const auto& edge:reusable_union.edges)
                for(const auto& reference:edge.references)
                    parents.edges.insert(reference_key(reference));
            for(const auto& vertex:reusable_union.vertices)
                for(const auto& reference:vertex.references)
                    parents.vertices.insert(reference_key(reference));
            const auto intersection_count=[](const auto& first,
                    const auto& second) {
                std::size_t count{};
                for(const auto& value:first)count+=second.contains(value);
                return count;
            };
            return std::tuple{expected,parents,
                intersection_count(expected.faces,parents.faces),
                intersection_count(expected.edges,parents.edges),
                intersection_count(expected.vertices,parents.vertices)};
        }();
        const auto& [expected_references,parent_references,
            retained_face_references,retained_edge_references,
            retained_vertex_references]=reference_sets;
        const auto report_missing_references=[&](std::string_view label,
                const auto& entries,const auto& parents) {
            std::map<std::tuple<std::size_t,std::string,std::string>,
                std::size_t> groups;
            for(const auto& [shape,reference]:entries) {
                static_cast<void>(shape);
                if(parents.contains(reference_key(reference)))continue;
                std::string role=reference.semantic_key;
                if(const auto first=role.find(':');first!=std::string::npos) {
                    const auto second=role.find(':',first+1);
                    const auto third=second==std::string::npos
                        ?std::string::npos:role.find(':',second+1);
                    role.resize(third==std::string::npos
                        ?role.size():third);
                }
                const auto position=operation_positions.find(reference.owner_id);
                groups[{position==operation_positions.end()
                        ?operations.size():position->second,
                    reference.owner_id,std::move(role)}]++;
            }
            for(const auto& [group,count]:groups) {
                const auto& [position,owner,role]=group;
                std::cerr<<"missing "<<label<<" operation=";
                if(position==operations.size())std::cerr<<"unknown";
                else std::cerr<<position;
                std::cerr<<" owner="<<owner<<" role="<<role
                    <<" count="<<count<<'\n';
            }
        };
        report_missing_references("face",expected_bound.faces,
            parent_references.faces);
        report_missing_references("edge",expected_bound.edges,
            parent_references.edges);
        report_missing_references("vertex",expected_bound.vertices,
            parent_references.vertices);
        TopTools_IndexedMapOfShape expected_faces,expected_edges;
        TopExp::MapShapes(expected_shape,TopAbs_FACE,expected_faces);
        TopExp::MapShapes(expected_shape,TopAbs_EDGE,expected_edges);
        const auto edge_intervals=[&](const auto& edges) {
            std::vector<CurveInterval> intervals;
            intervals.reserve(static_cast<std::size_t>(edges.Extent()));
            for(int index=1;index<=edges.Extent();++index) {
                double first_parameter{},last_parameter{};
                auto curve=BRep_Tool::Curve(TopoDS::Edge(edges(index)),
                    first_parameter,last_parameter);
                if(curve.IsNull()||last_parameter<=first_parameter)
                    continue;
                CurveInterval interval{std::move(curve),first_parameter,
                    last_parameter,{}};
                GeomAdaptor_Curve adaptor(interval.curve,
                    first_parameter,last_parameter);
                BndLib_Add3dCurve::Add(adaptor,
                    std::max(1.0e-7,tolerance),interval.bounds);
                intervals.push_back(std::move(interval));
            }
            return intervals;
        };
        const auto edge_coverage_start=Clock::now();
        const auto expected_edge_intervals=edge_intervals(expected_edges);
        const auto graph_edge_intervals=edge_intervals(
            reusable_union_edges);
        const bool report_edge_partitions=
            std::getenv("ZIMA_CPP_REPORT_EDGE_PARTITIONS")!=nullptr;
        const auto expected_edges_covered_by_graph=
            (treatment_only||report_edge_partitions)
            ?expected_edge_intervals.size()
            :static_cast<std::size_t>(std::ranges::count_if(
                expected_edge_intervals,[&](const auto& edge) {
                    return curve_is_covered_by_union(
                        edge,graph_edge_intervals);
                }));
        const auto graph_edges_covered_by_expected=
            (treatment_only||report_edge_partitions)
            ?graph_edge_intervals.size()
            :static_cast<std::size_t>(std::ranges::count_if(
                graph_edge_intervals,[&](const auto& edge) {
                    return curve_is_covered_by_union(
                        edge,expected_edge_intervals);
                }));
        if(report_edge_partitions) {
            const auto interval_for=[&](const TopoDS_Edge& edge)
                    ->std::optional<CurveInterval> {
                double first{},last{};
                auto curve=BRep_Tool::Curve(edge,first,last);
                if(curve.IsNull()||last<=first)return std::nullopt;
                CurveInterval interval{std::move(curve),first,last,{}};
                GeomAdaptor_Curve adaptor(interval.curve,first,last);
                BndLib_Add3dCurve::Add(adaptor,
                    std::max(1.0e-7,tolerance),interval.bounds);
                return interval;
            };
            for(int expected_index=1;
                    expected_index<=expected_edges.Extent();
                    ++expected_index) {
                const auto expected_edge=TopoDS::Edge(
                    expected_edges(expected_index));
                const auto expected_interval=interval_for(expected_edge);
                if(!expected_interval)continue;
                std::vector<CurveInterval> fragments;
                std::vector<int> fragment_indices;
                for(int graph_index=1;
                        graph_index<=reusable_union_edges.Extent();
                        ++graph_index) {
                    const auto graph_interval=interval_for(TopoDS::Edge(
                        reusable_union_edges(graph_index)));
                    if(!graph_interval||
                       !curve_is_covered(*graph_interval,
                            {*expected_interval}))continue;
                    fragments.push_back(*graph_interval);
                    fragment_indices.push_back(graph_index);
                }
                if(fragments.size()<2||!curve_is_covered_by_union(
                        *expected_interval,fragments))continue;
                std::cerr<<"direct edge partition expected_index="
                    <<expected_index<<" fragments="<<fragments.size();
                for(const auto& [shape,reference]:expected_bound.edges)
                    if(shape.IsSame(expected_edge))
                        std::cerr<<" expected_reference="
                            <<reference.owner_id<<':'
                            <<reference.semantic_key;
                for(const int graph_index:fragment_indices) {
                    const auto graph_edge=TopoDS::Edge(
                        reusable_union_edges(graph_index));
                    std::cerr<<" graph_index="<<graph_index;
                    const auto metadata=std::ranges::find_if(
                        reusable_union.edges,[&](const auto& edge) {
                            return edge.shape.IsSame(graph_edge);
                        });
                    if(metadata!=reusable_union.edges.end()) {
                        std::cerr<<" graph_parent_count="
                            <<metadata->references.size();
                        for(const auto& reference:metadata->references)
                            std::cerr<<" graph_parent="
                                <<reference.owner_id<<':'
                                <<reference.semantic_key;
                    }
                }
                std::cerr<<'\n';
            }
        }
        std::size_t treatment_requests{},treatment_mapped_references{},
            treatment_mapping_failures{};
        std::map<std::string,std::vector<kernel::EdgeReference>>
            treatment_edge_map;
        std::vector<CurveInterval> named_edge_intervals;
        named_edge_intervals.reserve(named_topology.edges.size());
        for(const auto& edge:named_topology.edges) {
            double first_parameter{},last_parameter{};
            auto curve=BRep_Tool::Curve(edge.shape,first_parameter,
                last_parameter);
            if(curve.IsNull()||last_parameter<=first_parameter) {
                named_edge_intervals.push_back({});
                continue;
            }
            CurveInterval interval{std::move(curve),first_parameter,
                last_parameter,{}};
            GeomAdaptor_Curve adaptor(interval.curve,
                first_parameter,last_parameter);
            BndLib_Add3dCurve::Add(adaptor,
                std::max(1.0e-7,tolerance),interval.bounds);
            named_edge_intervals.push_back(std::move(interval));
        }
        const auto map_treatment_edges=[&](const auto& treatment) {
            for(const auto& requested:treatment.edges) {
                ++treatment_requests;
                std::set<std::string> mapped;
                std::map<std::string,kernel::EdgeReference> mapped_references;
                bool found_old=false,covered=true;
                for(const auto& [shape,reference]:expected_bound.edges) {
                    if(reference!=requested)continue;
                    found_old=true;
                    double first_parameter{},last_parameter{};
                    auto curve=BRep_Tool::Curve(TopoDS::Edge(shape),
                        first_parameter,last_parameter);
                    if(curve.IsNull()||last_parameter<=first_parameter) {
                        covered=false;
                        continue;
                    }
                    CurveInterval old{std::move(curve),first_parameter,
                        last_parameter,{}};
                    GeomAdaptor_Curve adaptor(old.curve,
                        first_parameter,last_parameter);
                    BndLib_Add3dCurve::Add(adaptor,
                        std::max(1.0e-7,tolerance),old.bounds);
                    std::vector<CurveInterval> descendants;
                    for(std::size_t index=0;
                            index<named_edge_intervals.size();++index) {
                        const auto& candidate=named_edge_intervals[index];
                        if(candidate.curve.IsNull()||
                           !curve_is_covered_by_union(candidate,{old}))
                            continue;
                        descendants.push_back(candidate);
                        const auto& direct=
                            named_topology.edges[index].reference;
                        mapped.insert(reference_key(direct));
                        mapped_references.try_emplace(
                            reference_key(direct),direct);
                    }
                    covered=covered&&curve_is_covered_by_union(
                        old,descendants);
                }
                if(!found_old||!covered||mapped.empty())
                    ++treatment_mapping_failures;
                else {
                    auto& direct=treatment_edge_map[
                        reference_key(requested)];
                    for(const auto& [key,reference]:mapped_references)
                        if(std::ranges::none_of(direct,[&](const auto& current) {
                                return reference_key(current)==key;
                            }))
                            direct.push_back(reference);
                }
                treatment_mapped_references+=mapped.size();
            }
        };
        for(std::size_t index=last+1;index<operations.size();++index) {
            if(std::holds_alternative<kernel::FilletRequest>(
                    operations[index].primitive)||
               std::holds_alternative<kernel::ChamferRequest>(
                    operations[index].primitive))
                std::cerr<<"batch treatment index="<<index
                    <<" owner="<<operations[index].owner_id
                    <<" body="<<operations[index].body.id
                    <<" local="<<body_operation_index(index)<<'\n';
            if(const auto* fillet=std::get_if<kernel::FilletRequest>(
                    &operations[index].primitive))
                map_treatment_edges(*fillet);
            else if(const auto* chamfer=std::get_if<kernel::ChamferRequest>(
                    &operations[index].primitive))
                map_treatment_edges(*chamfer);
            else break;
        }
        double treatment_replay_ms=-1.,treatment_expected_minus=-1.,
            treatment_minus_expected=-1.,treatment_actual_volume=-1.,
            treatment_expected_volume=-1.,downstream_document_ms=-1.,
            downstream_max_volume_delta=-1.,downstream_block_ms=-1.,
            downstream_block_first_add_ms=-1.,
            downstream_block_subtract_ms=-1.,
            downstream_block_final_add_ms=-1.,
            downstream_block_last_single_ms=-1.,
            downstream_block_volume_delta=-1.;
        std::size_t treatment_replay_faces{},treatment_expected_faces{},
            downstream_output_count{},downstream_face_mismatches{},
            downstream_block_faces{},downstream_block_expected_faces{};
        std::string downstream_block_error;
        if(!treatment_mapping_failures&&published_block) {
            std::size_t treatment_last=last;
            auto remapped_operations=operations;
            while(treatment_last+1<remapped_operations.size()) {
                const auto& next=remapped_operations[treatment_last+1];
                if(std::holds_alternative<kernel::FilletRequest>(
                        next.primitive)||
                   std::holds_alternative<kernel::ChamferRequest>(
                        next.primitive))++treatment_last;
                else break;
            }
            if(treatment_last>last) {
                const bool nested=
                    previous.back().body_boundaries.contains(body_id);
                const auto complete_local=nested
                    ?previous.back().body_boundaries.at(body_id).get()
                    :previous;
                if(complete_local.size()>last) {
                    auto block=*published_block;
                    block.source_fingerprint=
                        complete_local[last].source_fingerprint;
                    if(complete_local.back().incremental_history&&
                       complete_local.back().incremental_history->frames.size()>last) {
                        auto history=
                            *complete_local.back().incremental_history;
                        auto frames=history.frames.get();
                        auto native=block;
                        native.incremental_history.reset();
                        frames[last].native_body=kernel::BodySnapshot(
                            std::move(native));
                        frames[last].hidden_edges.clear();
                        history.frames=std::move(frames);
                        block.incremental_history=
                            std::make_shared<const kernel::KernelHistoryCache>(
                                std::move(history));
                    }
                    auto prefix=complete_local;
                    prefix.resize(last+1);
                    // The direct calculation replaces every intermediate
                    // Boolean packet in this block. Retain only its authored
                    // prefix fingerprints; stale cached geometry or errors
                    // inside the replaced range must not force a serial replay.
                    for(std::size_t index=first;index<last;++index) {
                        const auto fingerprint=
                            prefix[index].source_fingerprint;
                        prefix[index]=kernel::BodyResult{};
                        prefix[index].source_fingerprint=fingerprint;
                    }
                    std::cerr<<"direct prefix boundary 13 errors="
                        <<prefix.at(13).calculation_errors.size()
                        <<" fingerprint="
                        <<prefix.at(13).source_fingerprint<<'\n';
                    prefix.back()=std::move(block);
                    std::vector<kernel::BodyResult> previous_prefix;
                    if(nested) {
                        auto previous_document=previous.back();
                        previous_document.body_boundaries.insert_or_assign(
                            body_id,std::move(prefix));
                        previous_prefix.push_back(
                            std::move(previous_document));
                    } else previous_prefix=std::move(prefix);
                    auto& treatment_kernel=*published_kernel;
                    const auto replay_start=Clock::now();
                    auto treated=std::move(previous_prefix);
                    const auto remap_between=[&](auto& treatment,
                            const kernel::BodyResult& expected_source,
                            const kernel::BodyResult& actual_source) {
                        if constexpr(std::is_same_v<
                                std::decay_t<decltype(treatment)>,
                                kernel::FilletRequest>)
                            if(treatment.mode!=
                                    kernel::FilletRequest::Mode::Constant)
                                throw std::runtime_error(
                                    "Direct Add replay requires vertex remapping for a variable Fillet");
                        const auto expected_edges=
                            bound_face_shape(expected_source);
                        const auto actual_edges=
                            bound_face_shape(actual_source);
                        if(expected_edges.root.IsNull()||
                           actual_edges.root.IsNull())
                            throw std::runtime_error(
                                "Direct Add replay has no persisted treatment input");
                        std::vector<CurveInterval> actual_intervals;
                        actual_intervals.reserve(actual_edges.edges.size());
                        for(const auto& [shape,reference]:actual_edges.edges) {
                            static_cast<void>(reference);
                            double first_parameter{},last_parameter{};
                            auto curve=BRep_Tool::Curve(
                                TopoDS::Edge(shape),first_parameter,
                                last_parameter);
                            if(curve.IsNull()||
                               last_parameter<=first_parameter) {
                                actual_intervals.push_back({});
                                continue;
                            }
                            CurveInterval interval{std::move(curve),
                                first_parameter,last_parameter,{}};
                            GeomAdaptor_Curve adaptor(interval.curve,
                                first_parameter,last_parameter);
                            BndLib_Add3dCurve::Add(adaptor,
                                std::max(1.0e-7,tolerance),
                                interval.bounds);
                            actual_intervals.push_back(
                                std::move(interval));
                        }
                        std::vector<kernel::EdgeReference> remapped;
                        for(const auto& requested:treatment.edges) {
                            bool found=false,covered=true;
                            std::vector<CurveInterval> descendants;
                            std::map<std::string,kernel::EdgeReference>
                                descendant_references;
                            for(const auto& [shape,reference]:
                                    expected_edges.edges) {
                                if(reference!=requested)continue;
                                found=true;
                                double first_parameter{},last_parameter{};
                                auto curve=BRep_Tool::Curve(
                                    TopoDS::Edge(shape),first_parameter,
                                    last_parameter);
                                if(curve.IsNull()||
                                   last_parameter<=first_parameter) {
                                    covered=false;continue;
                                }
                                CurveInterval source{std::move(curve),
                                    first_parameter,last_parameter,{}};
                                GeomAdaptor_Curve adaptor(source.curve,
                                    first_parameter,last_parameter);
                                BndLib_Add3dCurve::Add(adaptor,
                                    std::max(1.0e-7,tolerance),
                                    source.bounds);
                                for(std::size_t candidate=0;
                                        candidate<actual_intervals.size();
                                        ++candidate) {
                                    const auto& interval=
                                        actual_intervals[candidate];
                                    if(interval.curve.IsNull()||
                                       !curve_is_covered_by_union(
                                            interval,{source}))continue;
                                    descendants.push_back(interval);
                                    const auto& direct=
                                        actual_edges.edges[candidate].second;
                                    descendant_references.try_emplace(
                                        reference_key(direct),direct);
                                }
                                covered=covered&&
                                    curve_is_covered_by_union(
                                        source,descendants);
                            }
                            if(!found||!covered||
                               descendant_references.empty())
                                throw std::runtime_error(
                                    "Direct Add replay lost a treatment edge after an earlier treatment");
                            for(const auto& [key,reference]:
                                    descendant_references) {
                                static_cast<void>(key);
                                if(std::ranges::find(remapped,reference)==
                                        remapped.end())
                                    remapped.push_back(reference);
                            }
                        }
                        treatment.edges=std::move(remapped);
                    };
                    for(std::size_t index=last+1;
                            index<=treatment_last;++index) {
                        const auto& actual_input=index==last+1
                            ?*published_block:body_at(treated,index-1);
                        auto& next=remapped_operations[index];
                        if(auto* fillet=
                                std::get_if<kernel::FilletRequest>(
                                    &next.primitive))
                            remap_between(*fillet,local_body(index-1),
                                actual_input);
                        else if(auto* chamfer=
                                std::get_if<kernel::ChamferRequest>(
                                    &next.primitive))
                            remap_between(*chamfer,local_body(index-1),
                                actual_input);
                        auto step_operations=remapped_operations;
                        step_operations.resize(index+1);
                        treated=treatment_kernel.
                            evaluate_history_incremental(
                                step_operations,treated);
                        if(treated.size()!=index+1||
                           !treated.back().calculation_errors.empty())
                            throw std::runtime_error(
                                "Direct Add treatment replay did not calculate");
                    }
                    treatment_replay_ms=
                        std::chrono::duration<double,std::milli>(
                            Clock::now()-replay_start).count();
                    const auto actual=kernel::read_calculated_body_shape(
                        treated.back());
                    const auto expected=kernel::read_calculated_body_shape(
                        local_body(treatment_last));
                    TopTools_IndexedMapOfShape replay_faces;
                    TopExp::MapShapes(actual,TopAbs_FACE,replay_faces);
                    treatment_replay_faces=static_cast<std::size_t>(
                        replay_faces.Extent());
                    TopTools_IndexedMapOfShape expected_treatment_faces;
                    TopExp::MapShapes(expected,TopAbs_FACE,
                        expected_treatment_faces);
                    treatment_expected_faces=static_cast<std::size_t>(
                        expected_treatment_faces.Extent());
                    GProp_GProps actual_properties,expected_properties;
                    BRepGProp::VolumeProperties(actual,actual_properties);
                    BRepGProp::VolumeProperties(expected,expected_properties);
                    treatment_actual_volume=
                        std::abs(actual_properties.Mass());
                    treatment_expected_volume=
                        std::abs(expected_properties.Mass());
                    if(treatment_only) {
                        for(std::size_t index=last+1;
                                index<=treatment_last;++index) {
                            const auto actual_step=
                                kernel::read_calculated_body_shape(
                                    body_at(treated,index));
                            const auto expected_step=
                                kernel::read_calculated_body_shape(
                                    local_body(index));
                            TopTools_IndexedMapOfShape actual_step_faces,
                                expected_step_faces;
                            TopExp::MapShapes(actual_step,TopAbs_FACE,
                                actual_step_faces);
                            TopExp::MapShapes(expected_step,TopAbs_FACE,
                                expected_step_faces);
                            GProp_GProps actual_step_properties,
                                expected_step_properties;
                            BRepGProp::VolumeProperties(actual_step,
                                actual_step_properties);
                            BRepGProp::VolumeProperties(expected_step,
                                expected_step_properties);
                            std::cerr<<std::setprecision(17)
                                <<"direct treatment step="<<index
                                <<" actual_faces="
                                <<actual_step_faces.Extent()
                                <<" expected_faces="
                                <<expected_step_faces.Extent()
                                <<" actual_volume="
                                <<std::abs(actual_step_properties.Mass())
                                <<" expected_volume="
                                <<std::abs(expected_step_properties.Mass())
                                <<'\n';
                        }
                    }
                    const auto difference=[&](const TopoDS_Shape& left,
                            const TopoDS_Shape& right) {
                        BRepAlgoAPI_Cut cut(left,right);
                        cut.SetRunParallel(true);
                        cut.SetFuzzyValue(tolerance);
                        cut.Build();
                        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
                        GProp_GProps properties;
                        BRepGProp::VolumeProperties(cut.Shape(),properties);
                        return std::abs(properties.Mass());
                    };
                    if(!treatment_only) {
                        treatment_expected_minus=
                            difference(expected,actual);
                        treatment_minus_expected=
                            difference(actual,expected);
                    }
                    if(treatment_only&&treatment_last+7<
                            remapped_operations.size()) {
                        try {
                            const auto block_start=Clock::now();
                            auto block_shape=actual;
                            const auto calculate_tool=[&](
                                    std::size_t index,
                                    kernel::HistoryOperation operation,
                                    const kernel::BodyResult* current_input) {
                                std::vector<kernel::OrderedUpToPatch>
                                    external_sources;
                                std::set<std::string> captured_sources;
                                const auto capture_source=[&](
                                        const kernel::FaceReference& reference) {
                                    if(!reference.valid()||
                                       !captured_sources.insert(
                                            reference_key(reference)).second)
                                        return;
                                    auto source=current_input
                                        ?published_kernel->capture_reference_face(
                                            *current_input,reference)
                                        :published_kernel->
                                            capture_reference_face_history(
                                                complete_local,reference);
                                    kernel::OrderedUpToPatch patch;
                                    patch.source_reference=reference;
                                    patch.target.reference=reference;
                                    patch.native_source=std::make_shared<
                                        const kernel::OrderedUpToSource>(
                                            kernel::OrderedUpToSource{
                                                kernel::BodySnapshot(
                                                    std::move(source)),
                                                reference,{}});
                                    external_sources.push_back(
                                        std::move(patch));
                                };
                                const auto collect_extrusion=[&](
                                        const kernel::ExtrusionRequest& value) {
                                    if((value.extent==kernel::ExtrusionRequest::
                                                Extent::UpToPlane||
                                        value.extent==kernel::ExtrusionRequest::
                                                Extent::UpToSurface)&&
                                       !value.target_is_datum)
                                        capture_source(value.target_face);
                                    if(value.reverse_limit&&
                                       !value.reverse_limit->datum)
                                        capture_source(
                                            value.reverse_limit->reference);
                                    if(value.ordered_end_limit)
                                        for(const auto& patch:
                                                value.ordered_end_limit->patches)
                                            if(!patch.target.datum)
                                                capture_source(
                                                    patch.target.reference);
                                };
                                const auto collect_revolution=[&](
                                        const kernel::RevolutionRequest& value) {
                                    if(value.end_limit&&
                                       !value.end_limit->datum)
                                        capture_source(
                                            value.end_limit->reference);
                                    if(value.ordered_end_limit)
                                        for(const auto& patch:
                                                value.ordered_end_limit->patches)
                                            if(!patch.target.datum)
                                                capture_source(
                                                    patch.target.reference);
                                };
                                if(const auto* group=std::get_if<
                                        kernel::FeatureGroupRequest>(
                                            &operation.primitive))
                                    for(const auto& child:group->children)
                                        std::visit([&](const auto& value) {
                                            using Request=std::decay_t<
                                                decltype(value)>;
                                            if constexpr(std::is_same_v<Request,
                                                    kernel::ExtrusionRequest>)
                                                collect_extrusion(value);
                                            else if constexpr(std::is_same_v<
                                                    Request,kernel::
                                                        RevolutionRequest>)
                                                collect_revolution(value);
                                        },child);
                                const auto& references=current_input
                                    ?*current_input:local_body(index-1);
                                return published_kernel->calculate_feature_tool(
                                    std::move(operation),
                                    references,{},references,
                                    external_sources);
                            };
                            const auto report_block_stage=[&](
                                    std::string_view label,
                                    const TopoDS_Shape& stage,
                                    std::size_t expected_index) {
                                const auto expected_stage=
                                    kernel::read_calculated_body_shape(
                                        local_body(expected_index));
                                TopTools_IndexedMapOfShape stage_faces,
                                    expected_faces;
                                TopExp::MapShapes(stage,TopAbs_FACE,
                                    stage_faces);
                                TopExp::MapShapes(expected_stage,TopAbs_FACE,
                                    expected_faces);
                                GProp_GProps stage_properties,
                                    expected_properties;
                                BRepGProp::VolumeProperties(stage,
                                    stage_properties);
                                BRepGProp::VolumeProperties(expected_stage,
                                    expected_properties);
                                std::cerr<<std::setprecision(17)
                                    <<"direct downstream stage="<<label
                                    <<" actual_faces="
                                    <<stage_faces.Extent()
                                    <<" expected_faces="
                                    <<expected_faces.Extent()
                                    <<" volume_delta="<<std::abs(
                                        std::abs(stage_properties.Mass())-
                                        std::abs(expected_properties.Mass()))
                                    <<'\n';
                            };

                            const std::size_t first_add=treatment_last+1;
                            const auto first_add_start=Clock::now();
                            const auto& treated_current=
                                body_at(treated,treatment_last);
                            auto combined_first_add_result=calculate_tool(
                                first_add,remapped_operations[first_add],
                                &treated_current);
                            const std::array<kernel::BodyResult,1>
                                direct_first_add_tools{
                                    combined_first_add_result};
                            const std::array<std::size_t,1>
                                direct_first_add_indices{first_add};
                            auto direct_first_add_result=published_kernel->
                                calculate_add_block(treated_current,
                                    direct_first_add_tools,
                                    direct_first_add_indices,history_owners,
                                    remapped_operations[first_add].
                                        boolean_tolerance,
                                    remapped_operations[first_add].
                                        mesh_deflection,
                                    "boundary-probe:first-add",{},false);
                            downstream_block_first_add_ms=
                                std::chrono::duration<double,std::milli>(
                                    Clock::now()-first_add_start).count();
                            const std::size_t subtract_first=first_add+1;
                            std::size_t subtract_last=subtract_first;
                            while(subtract_last+1<
                                      remapped_operations.size()&&
                                  remapped_operations[subtract_last+1].
                                      operation==
                                      kernel::BooleanOperation::Subtract)
                                ++subtract_last;
                            const auto subtract_start=Clock::now();
                            kernel::OcctKernel subtract_kernel;
                            std::map<std::string,kernel::BodyResult>
                                subtract_sources;
                            std::vector<kernel::BodyResult>
                                subtract_tool_results;
                            std::vector<std::size_t>
                                subtract_tool_indices;
                            double cut_tolerance=1.0e-7;
                            for(std::size_t index=subtract_first;
                                    index<=subtract_last;++index) {
                                kernel::BodyResult tool;
                                const auto& operation=
                                    remapped_operations[index];
                                if(operation.feature_copy) {
                                    const auto& copy=
                                        *operation.feature_copy;
                                    const auto source=
                                        subtract_sources.find(
                                            copy.source_feature_id);
                                    if(source==subtract_sources.end())
                                        throw std::runtime_error(
                                            "Downstream block copy source is unavailable");
                                    tool=copy.combination==
                                            kernel::BodyCombination::Pattern
                                        ?subtract_kernel.pattern_body(
                                            source->second,copy.pattern,
                                            operation.owner_id)
                                        :subtract_kernel.mirror_body(
                                            source->second,
                                            copy.mirror_plane,
                                            operation.owner_id);
                                } else tool=calculate_tool(index,operation,
                                    &direct_first_add_result);
                                subtract_sources.insert_or_assign(
                                    operation.owner_id,tool);
                                subtract_tool_results.push_back(tool);
                                subtract_tool_indices.push_back(index);
                                cut_tolerance=std::max(cut_tolerance,
                                    operation.boolean_tolerance);
                            }
                            auto direct_subtract_result=published_kernel->
                                calculate_subtract_block(
                                    direct_first_add_result,
                                    subtract_tool_results,
                                    subtract_tool_indices,history_owners,
                                    cut_tolerance,
                                    remapped_operations[subtract_last].
                                        mesh_deflection,
                                    "boundary-probe:subtract",false);
                            downstream_block_subtract_ms=
                                std::chrono::duration<double,std::milli>(
                                    Clock::now()-subtract_start).count();
                            const auto final_add_start=Clock::now();
                            double final_tolerance=1.0e-7;
                            std::vector<kernel::BodyResult>
                                final_tool_results;
                            std::vector<std::size_t> final_tool_indices;
                            for(std::size_t index=subtract_last+1;
                                    index<remapped_operations.size();
                                    ++index) {
                                if(remapped_operations[index].operation!=
                                        kernel::BooleanOperation::Add)
                                    throw std::runtime_error(
                                        "Downstream block suffix is not additive");
                                final_tolerance=std::max(final_tolerance,
                                    remapped_operations[index].
                                        boolean_tolerance);
                                auto tool=calculate_tool(index,
                                    remapped_operations[index],
                                    &direct_subtract_result);
                                final_tool_results.push_back(
                                    std::move(tool));
                                final_tool_indices.push_back(index);
                            }
                            const auto direct_final_batch=published_kernel->
                                calculate_add_block(direct_subtract_result,
                                    final_tool_results,final_tool_indices,
                                    history_owners,final_tolerance,
                                    remapped_operations.back().mesh_deflection,
                                    "boundary-probe:final");
                            report_block_stage("final-add-boundary-batch",
                                kernel::read_calculated_body_shape(
                                    direct_final_batch),
                                remapped_operations.size()-1);
                            downstream_block_final_add_ms=
                                std::chrono::duration<double,std::milli>(
                                    Clock::now()-final_add_start).count();
                            downstream_block_ms=
                                std::chrono::duration<double,std::milli>(
                                    Clock::now()-block_start).count();

                            if(final_tool_results.size()==2) {
                                const std::span<const kernel::BodyResult>
                                    penultimate_tools(
                                        final_tool_results.data(),1);
                                const std::span<const std::size_t>
                                    penultimate_indices(
                                        final_tool_indices.data(),1);
                                auto penultimate=published_kernel->
                                    calculate_add_block(
                                        direct_subtract_result,
                                        penultimate_tools,
                                        penultimate_indices,history_owners,
                                        remapped_operations[
                                            final_tool_indices.front()].
                                                boolean_tolerance,
                                        remapped_operations[
                                            final_tool_indices.front()].
                                                mesh_deflection,
                                        "boundary-probe:penultimate",{},
                                        false);
                                const auto last_single_start=Clock::now();
                                auto last_tool=calculate_tool(
                                    final_tool_indices.back(),
                                    remapped_operations[
                                        final_tool_indices.back()],
                                    &penultimate);
                                const std::array<kernel::BodyResult,1>
                                    last_tools{std::move(last_tool)};
                                const std::array<std::size_t,1>
                                    last_indices{
                                        final_tool_indices.back()};
                                const auto last_single=published_kernel->
                                    calculate_add_block(
                                        penultimate,last_tools,last_indices,
                                        history_owners,
                                        remapped_operations[
                                            last_indices.front()].
                                                boolean_tolerance,
                                        remapped_operations[
                                            last_indices.front()].
                                                mesh_deflection,
                                        "boundary-probe:last-single");
                                downstream_block_last_single_ms=
                                    std::chrono::duration<double,std::milli>(
                                        Clock::now()-last_single_start).
                                            count();
                                report_block_stage(
                                    "last-single-boundary",
                                    kernel::read_calculated_body_shape(
                                        last_single),
                                    remapped_operations.size()-1);
                            }
                            const auto expected_final=
                                kernel::read_calculated_body_shape(
                                    local_body(
                                        remapped_operations.size()-1));
                            block_shape=kernel::read_calculated_body_shape(
                                direct_final_batch);
                            TopTools_IndexedMapOfShape block_faces,
                                expected_final_faces;
                            TopExp::MapShapes(block_shape,TopAbs_FACE,
                                block_faces);
                            TopExp::MapShapes(expected_final,TopAbs_FACE,
                                expected_final_faces);
                            downstream_block_faces=
                                static_cast<std::size_t>(
                                    block_faces.Extent());
                            downstream_block_expected_faces=
                                static_cast<std::size_t>(
                                    expected_final_faces.Extent());
                            GProp_GProps block_properties,
                                expected_final_properties;
                            BRepGProp::VolumeProperties(block_shape,
                                block_properties);
                            BRepGProp::VolumeProperties(expected_final,
                                expected_final_properties);
                            downstream_block_volume_delta=std::abs(
                                std::abs(block_properties.Mass())-
                                std::abs(expected_final_properties.Mass()));
                            std::cerr<<std::setprecision(17)
                                <<"direct downstream block faces="
                                <<downstream_block_faces
                                <<" expected_faces="
                                <<downstream_block_expected_faces
                                <<" volume_delta="
                                <<downstream_block_volume_delta<<'\n';
                        } catch(const std::exception& error) {
                            downstream_block_error=error.what();
                            std::cerr<<"direct downstream block failed: "
                                <<downstream_block_error<<'\n';
                        }
                    }
                    if(treatment_last+1<remapped_operations.size()) {
                        const auto downstream_start=Clock::now();
                        const auto downstream=treatment_kernel.
                            evaluate_history_incremental(
                                remapped_operations,treated);
                        downstream_document_ms=
                            std::chrono::duration<double,std::milli>(
                                Clock::now()-downstream_start).count();
                        if(downstream.size()!=remapped_operations.size()||
                           !downstream.back().calculation_errors.empty())
                            throw std::runtime_error(
                                "Direct Add downstream document replay did not calculate");
                        const auto& actual_outputs=
                            downstream.back().body_outputs;
                        const auto& expected_outputs=
                            previous.back().body_outputs;
                        downstream_output_count=expected_outputs.size();
                        downstream_max_volume_delta=0.;
                        for(const auto& [id,expected_snapshot]:
                                expected_outputs) {
                            const auto found=actual_outputs.find(id);
                            if(found==actual_outputs.end())
                                throw std::runtime_error(
                                    "Direct Add downstream replay lost a Body output");
                            const auto actual_shape=
                                kernel::read_calculated_body_shape(
                                    found->second.get());
                            const auto expected_shape=
                                kernel::read_calculated_body_shape(
                                    expected_snapshot.get());
                            TopTools_IndexedMapOfShape actual_faces,
                                expected_faces;
                            TopExp::MapShapes(actual_shape,TopAbs_FACE,
                                actual_faces);
                            TopExp::MapShapes(expected_shape,TopAbs_FACE,
                                expected_faces);
                            if(actual_faces.Extent()!=
                                    expected_faces.Extent())
                                ++downstream_face_mismatches;
                            GProp_GProps actual_properties,
                                expected_properties;
                            BRepGProp::VolumeProperties(actual_shape,
                                actual_properties);
                            BRepGProp::VolumeProperties(expected_shape,
                                expected_properties);
                            const auto volume_delta=std::abs(
                                std::abs(actual_properties.Mass())-
                                std::abs(expected_properties.Mass()));
                            downstream_max_volume_delta=std::max(
                                downstream_max_volume_delta,volume_delta);
                            if(treatment_only)
                                std::cerr<<std::setprecision(17)
                                    <<"direct downstream body="<<id
                                    <<" actual_faces="
                                    <<actual_faces.Extent()
                                    <<" expected_faces="
                                    <<expected_faces.Extent()
                                    <<" volume_delta="<<volume_delta
                                    <<'\n';
                        }
                    }
                }
            }
        }
        if(treatment_only) {
            std::cout<<std::setprecision(17)
                <<"boundary_treatment_probe first="<<first
                <<" last="<<last
                <<" tools="<<tools.size()
                <<" parallel_tool_ms="<<parallel_tool_ms
                <<" union_ms="<<reusable_union_ms
                <<" naming_ms="<<naming_ms
                <<" treatment_requests="<<treatment_requests
                <<" treatment_mapped_references="
                <<treatment_mapped_references
                <<" treatment_mapping_failures="
                <<treatment_mapping_failures
                <<" treatment_replay_ms="<<treatment_replay_ms
                <<" treatment_replay_faces="
                <<treatment_replay_faces
                <<" treatment_expected_faces="
                <<treatment_expected_faces
                <<" treatment_actual_volume="
                <<treatment_actual_volume
                <<" treatment_expected_volume="
                <<treatment_expected_volume
                <<" treatment_expected_minus="
                <<treatment_expected_minus
                <<" treatment_minus_expected="
                <<treatment_minus_expected
                <<" downstream_document_ms="
                <<downstream_document_ms
                <<" downstream_output_count="
                <<downstream_output_count
                <<" downstream_face_mismatches="
                <<downstream_face_mismatches
                <<" downstream_max_volume_delta="
                <<downstream_max_volume_delta
                <<" downstream_block_ms="<<downstream_block_ms
                <<" downstream_block_first_add_ms="
                <<downstream_block_first_add_ms
                <<" downstream_block_subtract_ms="
                <<downstream_block_subtract_ms
                <<" downstream_block_final_add_ms="
                <<downstream_block_final_add_ms
                <<" downstream_block_last_single_ms="
                <<downstream_block_last_single_ms
                <<" downstream_block_faces="
                <<downstream_block_faces
                <<" downstream_block_expected_faces="
                <<downstream_block_expected_faces
                <<" downstream_block_volume_delta="
                <<downstream_block_volume_delta
                <<" downstream_block_error="
                <<std::quoted(downstream_block_error)<<'\n';
            return treatment_mapping_failures?1:0;
        }
        const auto edge_coverage_ms=
            std::chrono::duration<double,std::milli>(
                Clock::now()-edge_coverage_start).count();
        GProp_GProps expected_surface,expected_volume;
        BRepGProp::SurfaceProperties(expected_shape,expected_surface);
        BRepGProp::VolumeProperties(expected_shape,expected_volume);
        const auto edge_cleanup_start=Clock::now();
        ShapeUpgrade_UnifySameDomain edge_cleanup(
            reusable_union.shape,true,false,false);
        edge_cleanup.SetLinearTolerance(tolerance);
        edge_cleanup.SetAngularTolerance(1.0e-9);
        edge_cleanup.Build();
        const auto edge_cleanup_ms=
            std::chrono::duration<double,std::milli>(
                Clock::now()-edge_cleanup_start).count();
        TopTools_IndexedMapOfShape edge_cleanup_faces,
            edge_cleanup_edges;
        TopExp::MapShapes(edge_cleanup.Shape(),TopAbs_FACE,
            edge_cleanup_faces);
        TopExp::MapShapes(edge_cleanup.Shape(),TopAbs_EDGE,
            edge_cleanup_edges);
        GProp_GProps edge_cleanup_surface,edge_cleanup_volume;
        BRepGProp::SurfaceProperties(edge_cleanup.Shape(),
            edge_cleanup_surface);
        BRepGProp::VolumeProperties(edge_cleanup.Shape(),
            edge_cleanup_volume);
        Bnd_Box material_bounds;
        BRepBndLib::Add(expected_shape,material_bounds,true);
        Standard_Real x_min{},y_min{},z_min{},x_max{},y_max{},z_max{};
        material_bounds.Get(x_min,y_min,z_min,x_max,y_max,z_max);
        BRepClass3d_SolidClassifier expected_classifier(expected_shape),
            graph_classifier(reusable_union.shape);
        const auto robust_inside=[&](BRepClass3d_SolidClassifier& classifier,
                const gp_Pnt& point) {
            classifier.Perform(point,tolerance);
            return classifier.State()==TopAbs_IN||
                classifier.State()==TopAbs_ON;
        };
        IntCurvesFace_ShapeIntersector expected_ray,graph_ray;
        expected_ray.Load(expected_shape,tolerance);
        graph_ray.Load(reusable_union.shape,tolerance);
        const std::array<gp_Dir,4> ray_directions{
            gp_Dir(1.,.3713906763541037,.1278314382941072),
            gp_Dir(.2174811048921037,1.,.4393819273014491),
            gp_Dir(.4938721098301713,.1827391049281037,1.),
            gp_Dir(-.7310948127301942,1.,.2837190482719037)};
        const auto ray_inside=[&](IntCurvesFace_ShapeIntersector& ray,
                const gp_Pnt& point)->std::optional<bool> {
            for(const auto& direction:ray_directions) {
                ray.PerformNearest(gp_Lin(point,direction),
                    std::max(2.*tolerance,1.0e-8),1.0e100);
                if(!ray.IsDone())continue;
                if(ray.NbPnt()==0)return false;
                const auto transition=ray.Transition(1);
                if(transition==IntCurveSurface_Tangent)continue;
                return transition==IntCurveSurface_Out;
            }
            return {};
        };
        std::size_t robust_checks{};
        const auto classify_pair=[&](const gp_Pnt& point,
                bool check_expected_orientation=false,
                bool check_graph_orientation=false,
                bool should_be_inside=false) {
            auto expected_inside=ray_inside(expected_ray,point);
            auto graph_inside=ray_inside(graph_ray,point);
            if(!expected_inside) {
                expected_inside=robust_inside(expected_classifier,point);
                ++robust_checks;
            }
            if(!graph_inside) {
                graph_inside=robust_inside(graph_classifier,point);
                ++robust_checks;
            }
            if(*expected_inside!=*graph_inside||
               (check_expected_orientation&&
                    *expected_inside!=should_be_inside)||
               (check_graph_orientation&&
                    *graph_inside!=should_be_inside)) {
                expected_inside=robust_inside(expected_classifier,point);
                graph_inside=robust_inside(graph_classifier,point);
                robust_checks+=2;
            }
            return std::pair{*expected_inside,*graph_inside};
        };
        std::size_t grid_samples{},grid_mismatches{};
        constexpr int grid_size=9;
        for(int x=0;x<grid_size;++x)
            for(int y=0;y<grid_size;++y)
                for(int z=0;z<grid_size;++z) {
                    const gp_Pnt point(
                        x_min+(x+.5)/grid_size*(x_max-x_min),
                        y_min+(y+.5)/grid_size*(y_max-y_min),
                        z_min+(z+.5)/grid_size*(z_max-z_min));
                    ++grid_samples;
                    const auto [expected_inside,graph_inside]=
                        classify_pair(point);
                    grid_mismatches+=expected_inside!=graph_inside;
                }
        std::size_t surface_samples{},surface_mismatches{},
            expected_orientation_errors{},graph_orientation_errors{},
            surface_sample_failures{},surface_edge_skips{};
        const auto verify_faces=[&](const auto& faces,bool expected_set) {
            for(const auto& entry:faces) {
                using Entry=std::remove_cvref_t<decltype(entry)>;
                TopoDS_Face face;
                if constexpr(std::is_same_v<Entry,TopoDS_Shape>)
                    face=TopoDS::Face(entry);
                else face=entry.shape;
                double u_min{},u_max{},v_min{},v_max{};
                BRepTools::UVBounds(face,u_min,u_max,v_min,v_max);
                std::optional<std::pair<double,double>> parameters;
                for(int grid=1;grid<=9&&!parameters;++grid)
                    for(int u=0;u<grid&&!parameters;++u)
                        for(int v=0;v<grid&&!parameters;++v) {
                            const gp_Pnt2d point(
                                u_min+(u+.5)/grid*(u_max-u_min),
                                v_min+(v+.5)/grid*(v_max-v_min));
                            BRepClass_FaceClassifier classifier(
                                face,point,tolerance,true);
                            if(classifier.State()==TopAbs_IN)
                                parameters.emplace(point.X(),point.Y());
                        }
                if(!parameters) {
                    ++surface_sample_failures;
                    continue;
                }
                BRepAdaptor_Surface surface(face,true);
                gp_Pnt point;
                gp_Vec du,dv;
                surface.D1(parameters->first,parameters->second,
                    point,du,dv);
                auto normal=du.Crossed(dv);
                if(normal.SquareMagnitude()<=1.0e-18) {
                    ++surface_sample_failures;
                    continue;
                }
                normal.Normalize();
                if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
                const double probe=std::max(1.0e-3,1000.*tolerance);
                bool near_edge=false;
                for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();
                        edge.Next()) {
                    double first_parameter{},last_parameter{};
                    const auto curve=BRep_Tool::Curve(
                        TopoDS::Edge(edge.Current()),first_parameter,
                        last_parameter);
                    if(curve.IsNull()||last_parameter<=first_parameter)
                        continue;
                    GeomAPI_ProjectPointOnCurve projection(point,curve,
                        first_parameter,last_parameter);
                    if(projection.NbPoints()&&
                       projection.LowerDistance()<=10.*probe) {
                        near_edge=true;
                        break;
                    }
                }
                if(near_edge) {
                    ++surface_edge_skips;
                    continue;
                }
                for(const double side:{-1.,1.}) {
                    const auto sample=point.Translated(
                        normal*(side*probe));
                    const auto [expected_inside,graph_inside]=classify_pair(
                        sample,expected_set,!expected_set,side<0.);
                    ++surface_samples;
                    surface_mismatches+=expected_inside!=graph_inside;
                    const bool orientation_error=(expected_set
                            ?expected_inside:graph_inside)!=(side<0.);
                    expected_orientation_errors+=
                        expected_set&&orientation_error;
                    graph_orientation_errors+=
                        !expected_set&&orientation_error;
                    if(!expected_set&&
                       (expected_inside!=graph_inside||orientation_error)) {
                        std::cerr<<std::setprecision(17)
                            <<"fast graph face mismatch area=";
                        GProp_GProps area;
                        BRepGProp::SurfaceProperties(face,area);
                        std::cerr<<std::abs(area.Mass())
                            <<" point="<<point.X()<<','<<point.Y()<<','
                            <<point.Z()<<" side="<<side
                            <<" expected_inside="<<expected_inside
                            <<" graph_inside="<<graph_inside;
                        if constexpr(!std::is_same_v<Entry,TopoDS_Shape>)
                            for(const auto& reference:entry.references)
                                std::cerr<<" reference="
                                    <<reference.owner_id<<':'
                                    <<reference.semantic_key;
                        std::cerr<<'\n';
                    }
                }
            }
        };
        std::vector<TopoDS_Shape> expected_face_shapes;
        expected_face_shapes.reserve(
            static_cast<std::size_t>(expected_faces.Extent()));
        for(int face=1;face<=expected_faces.Extent();++face)
            expected_face_shapes.push_back(expected_faces(face));
        verify_faces(expected_face_shapes,true);
        verify_faces(reusable_union.faces,false);
        std::cout<<std::setprecision(17)
            <<"boundary_union_fast first="<<first<<" last="<<last
            <<" tools="<<tools.size()
            <<" parallel_tool_ms="<<parallel_tool_ms
            <<" union_ms="<<reusable_union_ms
            <<" naming_ms="<<naming_ms
            <<" repeated_union_ms="<<repeated_union_ms
            <<" published_block_ms="<<published_block_ms
            <<" published_serialize_ms="<<published_serialize_ms
            <<" published_json_bytes="<<published_json_bytes
            <<" edge_cleanup_ms="<<edge_cleanup_ms
            <<" edge_coverage_ms="<<edge_coverage_ms
            <<" graph_ms="<<reusable_union.metrics.graph_ms
            <<" split_ms="<<reusable_union.metrics.split_ms
            <<" classify_ms="<<reusable_union.metrics.classify_ms
            <<" sew_ms="<<reusable_union.metrics.sew_ms
            <<" unify_ms="<<reusable_union.metrics.unify_ms
            <<" expected_faces="<<expected_faces.Extent()
            <<" graph_faces="<<reusable_union_faces.Extent()
            <<" expected_edges="<<expected_edges.Extent()
            <<" graph_edges="<<reusable_union_edges.Extent()
            <<" graph_vertices="<<reusable_union_vertices.Extent()
            <<" named_faces="<<named_topology.faces.size()
            <<" named_edges="<<named_topology.edges.size()
            <<" named_vertices="<<named_topology.vertices.size()
            <<" published_triangles="
            <<(published_block?published_block->mesh.triangles.size()/3:0)
            <<" named_face_digest="<<identity_digest(named_topology.faces)
            <<" named_edge_digest="<<identity_digest(named_topology.edges)
            <<" named_vertex_digest="
            <<identity_digest(named_topology.vertices)
            <<" inherited_edges="<<std::ranges::count_if(
                reusable_union.edges,[](const auto& edge) {
                    return !edge.references.empty();
                })
            <<" intersection_edges="<<std::ranges::count_if(
                reusable_union.edges,[](const auto& edge) {
                    return edge.references.empty();
                })
            <<" inherited_vertices="<<std::ranges::count_if(
                reusable_union.vertices,[](const auto& vertex) {
                    return !vertex.references.empty();
                })
            <<" intersection_vertices="<<std::ranges::count_if(
                reusable_union.vertices,[](const auto& vertex) {
                    return vertex.references.empty();
                })
            <<" expected_face_references="
            <<expected_references.faces.size()
            <<" parent_face_references="<<parent_references.faces.size()
            <<" retained_face_references="<<retained_face_references
            <<" expected_edge_references="
            <<expected_references.edges.size()
            <<" parent_edge_references="<<parent_references.edges.size()
            <<" retained_edge_references="<<retained_edge_references
            <<" expected_vertex_references="
            <<expected_references.vertices.size()
            <<" parent_vertex_references="
            <<parent_references.vertices.size()
            <<" retained_vertex_references="
            <<retained_vertex_references
            <<" expected_edge_intervals="
            <<expected_edge_intervals.size()
            <<" graph_edge_intervals="<<graph_edge_intervals.size()
            <<" expected_edges_covered_by_graph="
            <<expected_edges_covered_by_graph
            <<" graph_edges_covered_by_expected="
            <<graph_edges_covered_by_expected
            <<" treatment_requests="<<treatment_requests
            <<" treatment_mapped_references="
            <<treatment_mapped_references
            <<" treatment_mapping_failures="
            <<treatment_mapping_failures
            <<" treatment_replay_ms="<<treatment_replay_ms
            <<" treatment_replay_faces="<<treatment_replay_faces
            <<" treatment_expected_minus="<<treatment_expected_minus
            <<" treatment_minus_expected="<<treatment_minus_expected
            <<" unprotected_faces="
            <<reusable_union.metrics.unprotected_faces
            <<" unprotected_edges="
            <<reusable_union.metrics.unprotected_edges
            <<" protected_identity_edges="
            <<reusable_union.metrics.protected_identity_edges
            <<" cleanup_valid="
            <<BRepCheck_Analyzer(edge_cleanup.Shape()).IsValid()
            <<" cleanup_faces="<<edge_cleanup_faces.Extent()
            <<" cleanup_edges="<<edge_cleanup_edges.Extent()
            <<" cleanup_area="<<std::abs(edge_cleanup_surface.Mass())
            <<" cleanup_volume="<<std::abs(edge_cleanup_volume.Mass())
            <<" expected_area="<<std::abs(expected_surface.Mass())
            <<" graph_area="<<std::abs(reusable_union_surface.Mass())
            <<" expected_volume="<<std::abs(expected_volume.Mass())
            <<" graph_volume="<<std::abs(reusable_union_volume.Mass())
            <<" grid_samples="<<grid_samples
            <<" grid_mismatches="<<grid_mismatches
            <<" robust_checks="<<robust_checks
            <<" surface_samples="<<surface_samples
            <<" surface_mismatches="<<surface_mismatches
            <<" expected_orientation_errors="
            <<expected_orientation_errors
            <<" graph_orientation_errors="<<graph_orientation_errors
            <<" surface_sample_failures="<<surface_sample_failures
            <<" surface_edge_skips="<<surface_edge_skips<<'\n';
        return 0;
    }
    TopTools_IndexedMapOfShape reusable_face_map;
    for(const auto& face:reusable_graph_faces)
        reusable_face_map.Add(face.shape);
    std::vector<std::vector<const kernel::OcctBoundaryCurve*>>
        reusable_face_curves(
            static_cast<std::size_t>(reusable_face_map.Extent())+1);
    for(const auto& curve:reusable_graph_source_curves) {
        const int first_face=reusable_face_map.FindIndex(curve.first_face);
        const int second_face=reusable_face_map.FindIndex(curve.second_face);
        if(first_face)reusable_face_curves[
            static_cast<std::size_t>(first_face)].push_back(&curve);
        if(second_face)reusable_face_curves[
            static_cast<std::size_t>(second_face)].push_back(&curve);
    }
    std::size_t local_split_faces{},local_split_patches{};
    std::size_t face_boundary_curves_skipped{};
    struct LocalPatch {
        TopoDS_Face face;
        int source_face{};
    };
    std::vector<LocalPatch> local_patches;
    const auto curve_is_face_boundary=[&](
            const kernel::OcctBoundaryCurve& curve,
            const TopoDS_Face& face) {
        constexpr int samples=9;
        std::vector<std::tuple<occ::handle<Geom_Curve>,double,double>>
            boundaries;
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
            double first_parameter{},last_parameter{};
            auto geometry=BRep_Tool::Curve(TopoDS::Edge(edge.Current()),
                first_parameter,last_parameter);
            if(!geometry.IsNull()&&last_parameter>first_parameter)
                boundaries.emplace_back(std::move(geometry),
                    first_parameter,last_parameter);
        }
        for(int sample=0;sample<samples;++sample) {
            gp_Pnt point;
            const double parameter=curve.first_parameter+
                (curve.last_parameter-curve.first_parameter)*sample/
                    (samples-1.);
            curve.curve->D0(parameter,point);
            bool covered=false;
            for(const auto& [geometry,first_parameter,last_parameter]:
                    boundaries) {
                GeomAPI_ProjectPointOnCurve projection(point,geometry,
                    first_parameter,last_parameter);
                if(projection.NbPoints()&&projection.LowerDistance()<=
                        std::max(1.0e-6,2.*tolerance)) {
                    covered=true;break;
                }
            }
            if(!covered)return false;
        }
        return true;
    };
    const auto local_split_start=Clock::now();
    for(int face_index=1;face_index<=reusable_face_map.Extent();
            ++face_index) {
        const auto& curves=reusable_face_curves[
            static_cast<std::size_t>(face_index)];
        if(curves.empty()) {
            local_patches.push_back({TopoDS::Face(
                reusable_face_map(face_index)),face_index});
            continue;
        }
        ++local_split_faces;
        NCollection_List<TopoDS_Shape> arguments,split_edges;
        const auto face=TopoDS::Face(reusable_face_map(face_index));
        arguments.Append(face);
        for(const auto* curve:curves) {
            if(curve_is_face_boundary(*curve,face)) {
                ++face_boundary_curves_skipped;continue;
            }
            BRepBuilderAPI_MakeEdge edge(curve->curve,
                curve->first_parameter,curve->last_parameter);
            if(!edge.IsDone())continue;
            split_edges.Append(edge.Edge());
        }
        if(split_edges.IsEmpty()) {
            local_patches.push_back({face,face_index});
            continue;
        }
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(arguments);splitter.SetTools(split_edges);
        splitter.SetNonDestructive(true);splitter.SetRunParallel(false);
        splitter.SetFuzzyValue(tolerance);splitter.Build();
        if(!splitter.IsDone()||splitter.Shape().IsNull())
            throw std::runtime_error("Local boundary face split failed");
        TopTools_IndexedMapOfShape patches;
        TopExp::MapShapes(splitter.Shape(),TopAbs_FACE,patches);
        local_split_patches+=static_cast<std::size_t>(patches.Extent());
        for(int patch=1;patch<=patches.Extent();++patch)
            local_patches.push_back(
                {TopoDS::Face(patches(patch)),face_index});
        if(local_split_faces%64==0)
            std::cerr<<"local boundary faces split="<<local_split_faces
                <<" elapsed_ms="
                <<std::chrono::duration<double,std::milli>(
                    Clock::now()-local_split_start).count()<<'\n';
    }
    const auto local_split_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-local_split_start).count();
    std::vector<int> source_solid_by_face(
        static_cast<std::size_t>(reusable_face_map.Extent())+1,0);
    for(std::size_t tool=0;tool<reusable_graph_tool_faces.size();++tool)
        for(const auto& face:reusable_graph_tool_faces[tool]) {
            const int index=reusable_face_map.FindIndex(face.shape);
            if(index)source_solid_by_face[static_cast<std::size_t>(index)]=
                static_cast<int>(tool+1);
        }
    std::vector<TopoDS_Shape> source_solids{input_shape.root};
    source_solids.insert(source_solids.end(),tools.begin(),tools.end());
    std::vector<Bnd_Box> source_solid_bounds(source_solids.size());
    std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>>
        source_solid_classifiers;
    source_solid_classifiers.reserve(source_solids.size());
    for(std::size_t solid=0;solid<source_solids.size();++solid) {
        BRepBndLib::Add(source_solids[solid],source_solid_bounds[solid],true);
        source_solid_classifiers.push_back(
            std::make_unique<BRepClass3d_SolidClassifier>(
                source_solids[solid]));
    }
    const auto ray_load_start=Clock::now();
    std::vector<std::unique_ptr<IntCurvesFace_ShapeIntersector>>
        source_solid_ray_intersectors;
    source_solid_ray_intersectors.reserve(source_solids.size());
    for(const auto& solid:source_solids) {
        auto intersector=std::make_unique<IntCurvesFace_ShapeIntersector>();
        intersector->Load(solid,tolerance);
        source_solid_ray_intersectors.push_back(std::move(intersector));
    }
    const auto ray_load_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-ray_load_start).count();
    const std::array<gp_Dir,4> ray_directions{
        gp_Dir(1.,0.3713906763541037,0.1278314382941072),
        gp_Dir(0.2174811048921037,1.,0.4393819273014491),
        gp_Dir(0.4938721098301713,0.1827391049281037,1.),
        gp_Dir(-0.7310948127301942,1.,0.2837190482719037)};
    const auto ray_inside=[&](std::size_t solid,const gp_Pnt& point)
            ->std::optional<bool> {
        auto& intersector=*source_solid_ray_intersectors[solid];
        for(const auto& direction:ray_directions) {
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
    const auto face_area=[](const TopoDS_Face& face) {
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face,properties);
        return std::abs(properties.Mass());
    };
    double source_face_area{},split_patch_area{};
    for(int face=1;face<=reusable_face_map.Extent();++face)
        source_face_area+=face_area(TopoDS::Face(reusable_face_map(face)));
    for(const auto& patch:local_patches)
        split_patch_area+=face_area(patch.face);
    std::size_t patch_sample_failures{},retained_union_patches{};
    std::size_t internal_union_patches{},reversed_patch_normals{};
    std::size_t containment_queries{},ray_fallbacks{},ray_mismatches{};
    double ray_classify_ms{},solid_classifier_ms{};
    double retained_union_area{};
    std::vector<TopoDS_Face> retained_union_faces;
    std::vector<int> retained_union_source_faces;
    retained_union_faces.reserve(local_patches.size());
    retained_union_source_faces.reserve(local_patches.size());
    const auto classify_start=Clock::now();
    for(const auto& patch:local_patches) {
        double u_min{},u_max{},v_min{},v_max{};
        BRepTools::UVBounds(patch.face,u_min,u_max,v_min,v_max);
        BRepAdaptor_Surface surface(patch.face,true);
        std::optional<std::pair<double,double>> interior;
        for(int grid=1;grid<=9&&!interior;++grid)
            for(int u=0;u<grid&&!interior;++u)
                for(int v=0;v<grid&&!interior;++v) {
                    const double parameter_u=u_min+(u+.5)/grid*(u_max-u_min);
                    const double parameter_v=v_min+(v+.5)/grid*(v_max-v_min);
                    BRepClass_FaceClassifier classifier(patch.face,
                        gp_Pnt2d(parameter_u,parameter_v),tolerance,true);
                    if(classifier.State()==TopAbs_IN)
                        interior.emplace(parameter_u,parameter_v);
                }
        if(!interior) {
            ++patch_sample_failures;continue;
        }
        gp_Pnt point;gp_Vec derivative_u,derivative_v;
        surface.D1(interior->first,interior->second,point,
            derivative_u,derivative_v);
        auto normal=derivative_u.Crossed(derivative_v);
        if(normal.SquareMagnitude()<=1.0e-18) {
            ++patch_sample_failures;continue;
        }
        normal.Normalize();
        if(patch.face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        const double probe=std::max(1.0e-5,10.*tolerance);
        const int own_solid=source_solid_by_face[
            static_cast<std::size_t>(patch.source_face)];
        const auto outward=point.Translated(normal*probe);
        bool hidden=false;
        for(std::size_t solid=0;solid<source_solids.size();++solid) {
            if(static_cast<int>(solid)==own_solid||
               source_solid_bounds[solid].IsOut(outward))continue;
            ++containment_queries;
            const auto ray_start=Clock::now();
            const auto ray_state=ray_inside(solid,outward);
            ray_classify_ms+=std::chrono::duration<double,std::milli>(
                Clock::now()-ray_start).count();
            ray_fallbacks+=!ray_state;
            const auto classifier_start=Clock::now();
            source_solid_classifiers[solid]->Perform(outward,tolerance);
            const auto state=source_solid_classifiers[solid]->State();
            solid_classifier_ms+=std::chrono::duration<double,std::milli>(
                Clock::now()-classifier_start).count();
            const bool classifier_inside=state==TopAbs_IN||state==TopAbs_ON;
            if(ray_state&&*ray_state!=classifier_inside)++ray_mismatches;
            const bool inside=ray_state.value_or(classifier_inside);
            if(inside) {
                hidden=true;break;
            }
        }
        if(hidden)++internal_union_patches;
        else {
            ++retained_union_patches;
            retained_union_area+=face_area(patch.face);
            retained_union_faces.push_back(patch.face);
            retained_union_source_faces.push_back(patch.source_face);
        }
    }
    const auto classify_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-classify_start).count();
    const auto sew_start=Clock::now();
    BRepBuilderAPI_Sewing sewing(tolerance,true,true,true,false);
    for(const auto& face:retained_union_faces)sewing.Add(face);
    sewing.Perform();
    const auto sewed_shape=sewing.SewedShape();
    const auto sew_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-sew_start).count();
    TopTools_IndexedMapOfShape sewed_shells;
    if(!sewed_shape.IsNull())
        TopExp::MapShapes(sewed_shape,TopAbs_SHELL,sewed_shells);
    TopTools_IndexedMapOfShape sewed_faces;
    TopExp::MapShapes(sewed_shape,TopAbs_FACE,sewed_faces);
    std::vector<std::set<int>> sewed_face_sources(
        static_cast<std::size_t>(sewed_faces.Extent())+1);
    for(std::size_t index=0;index<retained_union_faces.size();++index) {
        const auto& input=retained_union_faces[index];
        const auto& output=sewing.IsModified(input)
            ?sewing.Modified(input):static_cast<const TopoDS_Shape&>(input);
        for(TopExp_Explorer face(output,TopAbs_FACE);face.More();face.Next()) {
            const int result=sewed_faces.FindIndex(face.Current());
            if(result)sewed_face_sources[static_cast<std::size_t>(result)]
                .insert(retained_union_source_faces[index]);
        }
    }
    BRepBuilderAPI_MakeSolid grouped_solid_builder;
    for(int shell=1;shell<=sewed_shells.Extent();++shell)
        grouped_solid_builder.Add(TopoDS::Shell(sewed_shells(shell)));
    auto grouped_solid=grouped_solid_builder.Solid();
    const bool grouped_solid_oriented=
        BRepLib::OrientClosedSolid(grouped_solid);
    BRepCheck_Analyzer grouped_solid_analyzer(grouped_solid);
    const bool grouped_solid_valid=sewed_shells.Extent()>0&&
        grouped_solid_analyzer.IsValid();
    const bool grouped_solid_topology_valid=sewed_shells.Extent()>0&&
        BRepCheck_Analyzer(grouped_solid,false).IsValid();
    std::map<BRepCheck_Status,std::size_t> grouped_solid_errors;
    for(int kind=static_cast<int>(TopAbs_SOLID);
            kind<=static_cast<int>(TopAbs_VERTEX);++kind) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(grouped_solid,
            static_cast<TopAbs_ShapeEnum>(kind),members);
        for(int member=1;member<=members.Extent();++member) {
            const auto result=grouped_solid_analyzer.Result(
                members(member));
            if(result.IsNull())continue;
            for(NCollection_List<BRepCheck_Status>::Iterator status(
                    result->Status());status.More();status.Next())
                if(status.Value()!=BRepCheck_NoError) {
                    ++grouped_solid_errors[status.Value()];
                    std::cerr<<"grouped solid invalid kind="<<kind
                        <<" member="<<member;
                    BRepCheck::Print(status.Value(),std::cerr);
                    if(kind==static_cast<int>(TopAbs_FACE)) {
                        const int sewed=sewed_faces.FindIndex(
                            members(member));
                        if(sewed) {
                            std::cerr<<" sources=";
                            for(const int source:sewed_face_sources[
                                    static_cast<std::size_t>(sewed)]) {
                                std::size_t transverse{},coplanar{};
                                for(const auto* curve:reusable_face_curves[
                                        static_cast<std::size_t>(source)])
                                    (curve->kind==kernel::
                                        OcctBoundaryCurveKind::Transverse
                                        ?transverse:coplanar)++;
                                std::cerr<<source<<"(solid="
                                    <<source_solid_by_face[
                                        static_cast<std::size_t>(source)]
                                    <<",T="<<transverse<<",C="<<coplanar
                                    <<",valid="
                                    <<BRepCheck_Analyzer(TopoDS::Face(
                                        reusable_face_map(source))).IsValid()
                                    <<",refs=";
                                for(const auto& candidate:
                                        reusable_graph_faces)
                                    if(candidate.shape.IsSame(
                                            reusable_face_map(source))) {
                                        for(const auto& reference:
                                                candidate.references)
                                            std::cerr<<reference.owner_id<<":"
                                                <<reference.semantic_key<<";";
                                        break;
                                    }
                                std::cerr<<"),";
                            }
                        }
                        std::size_t wires{};
                        for(TopExp_Explorer wire(members(member),TopAbs_WIRE);
                                wire.More();wire.Next())++wires;
                        std::cerr<<" wires="<<wires
                            <<" area="<<face_area(TopoDS::Face(
                                members(member)));
                    }
                    std::cerr<<'\n';
                }
        }
    }
    for(const auto& [status,count]:grouped_solid_errors) {
        std::cerr<<"grouped solid validation ";
        BRepCheck::Print(status,std::cerr);
        std::cerr<<" count="<<count<<'\n';
    }
    const auto solid_fix_start=Clock::now();
    ShapeFix_Solid solid_fixer(grouped_solid);
    solid_fixer.SetPrecision(tolerance);
    solid_fixer.SetMinTolerance(tolerance);
    solid_fixer.SetMaxTolerance(std::max(100.*tolerance,1.0e-4));
    solid_fixer.Perform();
    const auto fixed_union=solid_fixer.Shape();
    const auto solid_fix_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-solid_fix_start).count();
    const bool fixed_union_valid=!fixed_union.IsNull()&&
        BRepCheck_Analyzer(fixed_union).IsValid();
    TopTools_IndexedMapOfShape fixed_union_faces,fixed_union_edges,
        fixed_union_shells,fixed_union_solids;
    TopExp::MapShapes(fixed_union,TopAbs_FACE,fixed_union_faces);
    TopExp::MapShapes(fixed_union,TopAbs_EDGE,fixed_union_edges);
    TopExp::MapShapes(fixed_union,TopAbs_SHELL,fixed_union_shells);
    TopExp::MapShapes(fixed_union,TopAbs_SOLID,fixed_union_solids);
    GProp_GProps fixed_union_surface,fixed_union_volume;
    BRepGProp::SurfaceProperties(fixed_union,fixed_union_surface);
    BRepGProp::VolumeProperties(fixed_union,fixed_union_volume);
    BRep_Builder result_builder;
    TopoDS_Compound classified_union;
    result_builder.MakeCompound(classified_union);
    std::size_t oriented_solids{};
    for(int shell=1;shell<=sewed_shells.Extent();++shell) {
        BRepBuilderAPI_MakeSolid make_solid(
            TopoDS::Shell(sewed_shells(shell)));
        auto solid=make_solid.Solid();
        if(BRepLib::OrientClosedSolid(solid))++oriented_solids;
        result_builder.Add(classified_union,solid);
    }
    const bool classified_union_valid=sewed_shells.Extent()>0&&
        BRepCheck_Analyzer(classified_union).IsValid();
    TopTools_IndexedMapOfShape classified_union_faces,
        classified_union_edges;
    TopExp::MapShapes(classified_union,TopAbs_FACE,
        classified_union_faces);
    TopExp::MapShapes(classified_union,TopAbs_EDGE,
        classified_union_edges);
    GProp_GProps classified_union_surface,classified_union_volume;
    BRepGProp::SurfaceProperties(classified_union,
        classified_union_surface);
    BRepGProp::VolumeProperties(classified_union,
        classified_union_volume);
    GProp_GProps grouped_solid_surface,grouped_solid_volume;
    BRepGProp::SurfaceProperties(grouped_solid,grouped_solid_surface);
    BRepGProp::VolumeProperties(grouped_solid,grouped_solid_volume);
    std::vector<std::string> source_face_identity(
        static_cast<std::size_t>(reusable_face_map.Extent())+1);
    std::vector<std::size_t> source_face_history(
        static_cast<std::size_t>(reusable_face_map.Extent())+1,
        std::numeric_limits<std::size_t>::max());
    std::vector<std::optional<kernel::FaceReference>>
        source_face_reference(source_face_identity.size());
    for(const auto& source:reusable_graph_faces) {
        const int index=reusable_face_map.FindIndex(source.shape);
        if(!index||source.references.empty())continue;
        const auto candidate=reference_key(source.references.front());
        auto& current=source_face_identity[static_cast<std::size_t>(index)];
        auto& current_history=source_face_history[
            static_cast<std::size_t>(index)];
        if(current.empty()||std::tuple{source.history_index,candidate}<
                std::tuple{current_history,current}) {
            current=candidate;
            current_history=source.history_index;
            source_face_reference[static_cast<std::size_t>(index)]=
                source.references.front();
        }
    }
    std::vector<std::string> sewed_face_identity(
        static_cast<std::size_t>(sewed_faces.Extent())+1);
    std::vector<std::optional<kernel::FaceReference>>
        sewed_face_reference(sewed_face_identity.size());
    std::size_t sewed_faces_without_identity{};
    for(int face=1;face<=sewed_faces.Extent();++face) {
        std::vector<std::pair<std::size_t,std::string>> candidates;
        for(const int source:sewed_face_sources[static_cast<std::size_t>(face)]) {
            const auto& token=source_face_identity[
                static_cast<std::size_t>(source)];
            if(token.empty())continue;
            candidates.emplace_back(source_face_history[
                static_cast<std::size_t>(source)],token);
        }
        if(candidates.empty()) {
            ++sewed_faces_without_identity;continue;
        }
        const auto canonical=std::ranges::min(candidates);
        sewed_face_identity[static_cast<std::size_t>(face)]=
            canonical.second;
        for(const int source:sewed_face_sources[static_cast<std::size_t>(face)])
            if(source_face_history[static_cast<std::size_t>(source)]==
                    canonical.first&&
               source_face_identity[static_cast<std::size_t>(source)]==
                    canonical.second) {
                sewed_face_reference[static_cast<std::size_t>(face)]=
                    source_face_reference[static_cast<std::size_t>(source)];
                break;
            }
    }
    const auto graph_unify_start=Clock::now();
    ShapeUpgrade_UnifySameDomain graph_unify_probe(
        grouped_solid,true,true,false);
    graph_unify_probe.SetLinearTolerance(tolerance);
    graph_unify_probe.SetAngularTolerance(1.0e-9);
    graph_unify_probe.Build();
    TopTools_IndexedMapOfShape graph_unify_probe_edges;
    TopExp::MapShapes(graph_unify_probe.Shape(),TopAbs_EDGE,
        graph_unify_probe_edges);
    const auto probe_edge_survives=[&](const TopoDS_Shape& edge) {
        if(graph_unify_probe_edges.Contains(edge))return true;
        const auto history=graph_unify_probe.History();
        if(history.IsNull())return false;
        for(const auto* descendants:{&history->Modified(edge),
                &history->Generated(edge)})
            for(TopTools_ListIteratorOfListOfShape descendant(
                    *descendants);descendant.More();descendant.Next())
                if(descendant.Value().ShapeType()==TopAbs_EDGE&&
                   graph_unify_probe_edges.Contains(descendant.Value()))
                    return true;
        return false;
    };
    ShapeUpgrade_UnifySameDomain graph_unify(
        grouped_solid,true,true,false);
    graph_unify.SetLinearTolerance(tolerance);
    graph_unify.SetAngularTolerance(1.0e-9);
    TopTools_IndexedDataMapOfShapeListOfShape graph_edge_faces;
    TopExp::MapShapesAndAncestors(grouped_solid,TopAbs_EDGE,
        TopAbs_FACE,graph_edge_faces);
    std::size_t identity_boundary_edges_kept{};
    std::size_t same_reference_edges_merged{},
        coplanar_role_edges_merged{};
    for(int edge=1;edge<=graph_edge_faces.Extent();++edge) {
        const auto& adjacent=graph_edge_faces.FindFromIndex(edge);
        if(adjacent.Extent()!=2)continue;
        TopTools_ListIteratorOfListOfShape face(adjacent);
        const auto first_face=TopoDS::Face(face.Value());
        const int first_index=sewed_faces.FindIndex(first_face);
        face.Next();
        const auto second_face=TopoDS::Face(face.Value());
        const int second_index=sewed_faces.FindIndex(second_face);
        bool may_merge=first_index&&second_index;
        if(may_merge) {
            const auto& first=sewed_face_reference[
                static_cast<std::size_t>(first_index)];
            const auto& second=sewed_face_reference[
                static_cast<std::size_t>(second_index)];
            may_merge=first&&second;
            if(may_merge&&*first==*second) {
                ++same_reference_edges_merged;
            } else if(may_merge&&
                    first->surface_result==second->surface_result&&
                    first->sheet_role==second->sheet_role) {
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
                    if(may_merge)++coplanar_role_edges_merged;
                }
            } else may_merge=false;
        }
        if(!may_merge&&!probe_edge_survives(
                graph_edge_faces.FindKey(edge))) {
            graph_unify.KeepShape(graph_edge_faces.FindKey(edge));
            ++identity_boundary_edges_kept;
        }
    }
    graph_unify.Build();
    const auto unified_graph_union=graph_unify.Shape();
    const auto graph_unify_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-graph_unify_start).count();
    const bool unified_graph_valid=!unified_graph_union.IsNull()&&
        BRepCheck_Analyzer(unified_graph_union).IsValid();
    TopTools_IndexedMapOfShape unified_graph_faces,unified_graph_edges,
        unified_graph_shells,unified_graph_solids;
    TopExp::MapShapes(unified_graph_union,TopAbs_FACE,
        unified_graph_faces);
    TopExp::MapShapes(unified_graph_union,TopAbs_EDGE,
        unified_graph_edges);
    TopExp::MapShapes(unified_graph_union,TopAbs_SHELL,
        unified_graph_shells);
    TopExp::MapShapes(unified_graph_union,TopAbs_SOLID,
        unified_graph_solids);
    GProp_GProps unified_graph_surface,unified_graph_volume;
    BRepGProp::SurfaceProperties(unified_graph_union,
        unified_graph_surface);
    BRepGProp::VolumeProperties(unified_graph_union,
        unified_graph_volume);
    std::vector<std::set<int>> unified_graph_face_sources(
        static_cast<std::size_t>(unified_graph_faces.Extent())+1);
    const auto graph_unify_history=graph_unify.History();
    for(int face=1;face<=sewed_faces.Extent();++face) {
        std::vector<TopoDS_Shape> descendants;
        if(!graph_unify_history.IsNull())
            for(const auto* list:{&graph_unify_history->Modified(
                    sewed_faces(face)),&graph_unify_history->Generated(
                    sewed_faces(face))})
                for(TopTools_ListIteratorOfListOfShape descendant(*list);
                        descendant.More();descendant.Next())
                    if(descendant.Value().ShapeType()==TopAbs_FACE)
                        descendants.push_back(descendant.Value());
        if(descendants.empty()&&(graph_unify_history.IsNull()||
                !graph_unify_history->IsRemoved(sewed_faces(face))))
            descendants.push_back(sewed_faces(face));
        for(const auto& descendant:descendants) {
            const int target=unified_graph_faces.FindIndex(descendant);
            if(!target)continue;
            auto& sources=unified_graph_face_sources[
                static_cast<std::size_t>(target)];
            sources.insert(sewed_face_sources[
                static_cast<std::size_t>(face)].begin(),
                sewed_face_sources[static_cast<std::size_t>(face)].end());
        }
    }
    std::size_t unified_faces_without_source{},
        unified_faces_with_multiple_sources{};
    std::set<std::string> graph_face_aliases;
    for(int face=1;face<=unified_graph_faces.Extent();++face) {
        const auto& sources=unified_graph_face_sources[
            static_cast<std::size_t>(face)];
        unified_faces_without_source+=sources.empty();
        unified_faces_with_multiple_sources+=sources.size()>1;
        for(const int source:sources)
            for(const auto& metadata:reusable_graph_faces)
                if(metadata.shape.IsSame(reusable_face_map(source))) {
                    for(const auto& reference:metadata.references)
                        graph_face_aliases.insert(reference_key(reference));
                    break;
                }
    }
    const auto expected_probe_bound=bound_face_shape(local_body(last));
    std::set<std::string> expected_graph_face_references;
    for(const auto& [face,reference]:expected_probe_bound.faces) {
        static_cast<void>(face);
        expected_graph_face_references.insert(reference_key(reference));
    }
    std::vector<std::string> expected_face_references_without_source_alias;
    std::ranges::set_difference(expected_graph_face_references,
        graph_face_aliases,std::back_inserter(
            expected_face_references_without_source_alias));
    const auto expected_probe_shape=
        kernel::read_calculated_body_shape(local_body(last));
    GProp_GProps expected_probe_surface;
    BRepGProp::SurfaceProperties(expected_probe_shape,
        expected_probe_surface);
    const double expected_union_area=std::abs(
        expected_probe_surface.Mass());
    GProp_GProps expected_probe_volume;
    BRepGProp::VolumeProperties(expected_probe_shape,
        expected_probe_volume);
    const double expected_union_volume=std::abs(
        expected_probe_volume.Mass());
    TopTools_IndexedMapOfShape expected_probe_shells,
        expected_probe_solids,expected_probe_faces,expected_probe_edges;
    TopExp::MapShapes(expected_probe_shape,TopAbs_SHELL,
        expected_probe_shells);
    TopExp::MapShapes(expected_probe_shape,TopAbs_SOLID,
        expected_probe_solids);
    TopExp::MapShapes(expected_probe_shape,TopAbs_FACE,
        expected_probe_faces);
    TopExp::MapShapes(expected_probe_shape,TopAbs_EDGE,
        expected_probe_edges);
    const auto material_difference=[&](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        TopTools_ListOfShape arguments,tools;
        arguments.Append(left);tools.Append(right);
        BRepAlgoAPI_Cut cut;
        cut.SetArguments(arguments);
        cut.SetTools(tools);
        cut.SetRunParallel(true);
        cut.SetFuzzyValue(tolerance);
        cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto material_check_start=Clock::now();
    const double expected_minus_graph=material_difference(
        expected_probe_shape,reusable_union.shape);
    const double graph_minus_expected=material_difference(
        reusable_union.shape,expected_probe_shape);
    const auto material_check_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-material_check_start).count();
    Bnd_Box material_bounds;
    BRepBndLib::Add(expected_probe_shape,material_bounds,true);
    Standard_Real material_x_min{},material_y_min{},material_z_min{},
        material_x_max{},material_y_max{},material_z_max{};
    material_bounds.Get(material_x_min,material_y_min,material_z_min,
        material_x_max,material_y_max,material_z_max);
    BRepClass3d_SolidClassifier expected_material(expected_probe_shape),
        graph_material(reusable_union.shape);
    std::size_t material_point_samples{},material_point_mismatches{};
    constexpr int material_grid=9;
    for(int x=0;x<material_grid;++x)
        for(int y=0;y<material_grid;++y)
            for(int z=0;z<material_grid;++z) {
                const gp_Pnt point(
                    material_x_min+(x+.5)/material_grid*
                        (material_x_max-material_x_min),
                    material_y_min+(y+.5)/material_grid*
                        (material_y_max-material_y_min),
                    material_z_min+(z+.5)/material_grid*
                        (material_z_max-material_z_min));
                expected_material.Perform(point,tolerance);
                graph_material.Perform(point,tolerance);
                const bool expected_inside=
                    expected_material.State()==TopAbs_IN||
                    expected_material.State()==TopAbs_ON;
                const bool graph_inside=graph_material.State()==TopAbs_IN||
                    graph_material.State()==TopAbs_ON;
                ++material_point_samples;
                material_point_mismatches+=expected_inside!=graph_inside;
            }
    std::size_t material_surface_samples{},
        material_surface_mismatches{},material_surface_sample_failures{},
        material_surface_edge_skips{},expected_surface_mismatches{},
        graph_surface_mismatches{},expected_self_orientation_errors{},
        graph_self_orientation_errors{};
    std::set<std::string> graph_orientation_error_references;
    IntCurvesFace_ShapeIntersector expected_surface_intersector,
        graph_surface_intersector;
    expected_surface_intersector.Load(expected_probe_shape,tolerance);
    graph_surface_intersector.Load(reusable_union.shape,tolerance);
    const std::array<gp_Dir,4> surface_directions{
        gp_Dir(1.,.3713906763541037,.1278314382941072),
        gp_Dir(.2174811048921037,1.,.4393819273014491),
        gp_Dir(.4938721098301713,.1827391049281037,1.),
        gp_Dir(-.7310948127301942,1.,.2837190482719037)};
    const auto ray_material=[&](IntCurvesFace_ShapeIntersector& intersector,
            const gp_Pnt& sample)->std::optional<bool> {
        for(const auto& direction:surface_directions) {
            intersector.PerformNearest(gp_Lin(sample,direction),
                std::max(2.*tolerance,1.0e-8),1.0e100);
            if(!intersector.IsDone())continue;
            if(intersector.NbPnt()==0)return false;
            const auto transition=intersector.Transition(1);
            if(transition==IntCurveSurface_Tangent)continue;
            return transition==IntCurveSurface_Out;
        }
        return std::nullopt;
    };
    const auto compare_face_sides=[&](const TopoDS_Face& face,
            bool expected_face,
            const kernel::OcctBoundaryUnionFace* metadata=nullptr) {
        double u_min{},u_max{},v_min{},v_max{};
        BRepTools::UVBounds(face,u_min,u_max,v_min,v_max);
        std::optional<std::pair<double,double>> interior;
        for(int grid=1;grid<=9&&!interior;++grid)
            for(int u=0;u<grid&&!interior;++u)
                for(int v=0;v<grid&&!interior;++v) {
                    const double parameter_u=u_min+(u+.5)/grid*(u_max-u_min);
                    const double parameter_v=v_min+(v+.5)/grid*(v_max-v_min);
                    BRepClass_FaceClassifier classifier(face,
                        gp_Pnt2d(parameter_u,parameter_v),tolerance,true);
                    if(classifier.State()==TopAbs_IN)
                        interior.emplace(parameter_u,parameter_v);
                }
        if(!interior) {
            ++material_surface_sample_failures;
            return;
        }
        BRepAdaptor_Surface surface(face,true);
        gp_Pnt point;
        gp_Vec derivative_u,derivative_v;
        surface.D1(interior->first,interior->second,point,
            derivative_u,derivative_v);
        auto normal=derivative_u.Crossed(derivative_v);
        if(normal.SquareMagnitude()<=1.0e-18) {
            ++material_surface_sample_failures;
            return;
        }
        normal.Normalize();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        const double probe=std::max(1.0e-3,1000.*tolerance);
        bool near_edge=false;
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next()) {
            double first{},last{};
            const auto geometry=BRep_Tool::Curve(
                TopoDS::Edge(edge.Current()),first,last);
            if(geometry.IsNull()||last<=first)continue;
            GeomAPI_ProjectPointOnCurve projection(
                point,geometry,first,last);
            if(projection.NbPoints()&&
               projection.LowerDistance()<=10.*probe) {
                near_edge=true;
                break;
            }
        }
        if(near_edge) {
            ++material_surface_edge_skips;
            return;
        }
        for(const double side:{-1.,1.}) {
            const auto sample=point.Translated(normal*(side*probe));
            auto expected_inside=ray_material(
                expected_surface_intersector,sample);
            auto graph_inside=ray_material(graph_surface_intersector,sample);
            if(!expected_inside) {
                expected_material.Perform(sample,tolerance);
                expected_inside=expected_material.State()==TopAbs_IN||
                    expected_material.State()==TopAbs_ON;
            }
            if(!graph_inside) {
                graph_material.Perform(sample,tolerance);
                graph_inside=graph_material.State()==TopAbs_IN||
                    graph_material.State()==TopAbs_ON;
            }
            ++material_surface_samples;
            const bool mismatch=*expected_inside!=*graph_inside;
            material_surface_mismatches+=mismatch;
            expected_surface_mismatches+=expected_face&&mismatch;
            graph_surface_mismatches+=!expected_face&&mismatch;
            const bool should_be_inside=side<0.;
            expected_self_orientation_errors+=expected_face&&
                *expected_inside!=should_be_inside;
            graph_self_orientation_errors+=!expected_face&&
                *graph_inside!=should_be_inside;
            if(!expected_face&&metadata&&
               *graph_inside!=should_be_inside) {
                expected_material.Perform(sample,tolerance);
                const auto expected_classifier_state=
                    expected_material.State();
                graph_material.Perform(sample,tolerance);
                const auto graph_classifier_state=graph_material.State();
                std::cerr<<std::setprecision(17)
                    <<"graph face side error area="<<face_area(face)
                    <<" point="<<point.X()<<','<<point.Y()<<','<<point.Z()
                    <<" normal="<<normal.X()<<','<<normal.Y()<<','
                    <<normal.Z()<<" side="<<side
                    <<" expected_inside="<<*expected_inside
                    <<" graph_inside="<<*graph_inside
                    <<" expected_classifier="
                    <<static_cast<int>(expected_classifier_state)
                    <<" graph_classifier="
                    <<static_cast<int>(graph_classifier_state)
                    <<" orientation="<<static_cast<int>(face.Orientation());
                for(const auto& reference:metadata->references)
                    std::cerr<<" reference="<<reference.owner_id<<':'
                        <<reference.semantic_key;
                std::cerr<<'\n';
                for(const auto& reference:metadata->references)
                    graph_orientation_error_references.insert(
                        reference_key(reference));
            }
        }
    };
    for(int face=1;face<=expected_probe_faces.Extent();++face)
        compare_face_sides(TopoDS::Face(expected_probe_faces(face)),true);
    for(const auto& face:reusable_union.faces)
        compare_face_sides(face.shape,false,&face);
    std::string graph_orientation_error_reference_text;
    for(const auto& reference:graph_orientation_error_references) {
        if(!graph_orientation_error_reference_text.empty())
            graph_orientation_error_reference_text+='|';
        graph_orientation_error_reference_text+=reference;
    }
    const auto trimmed_intervals_covered_by_section=std::ranges::count_if(
        spatial_trimmed_curve_intervals,[&](const auto& interval) {
            return curve_is_covered(interval,section_curve_intervals);
        });
    const auto coverage_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-coverage_start).count();
    if(spatial_only) {
        std::cout<<std::setprecision(17)
            <<"boundary_graph_probe first="<<first<<" last="<<last
            <<" tools="<<tools.size()
            <<" tool_ms="<<tool_ms
            <<" parallel_tool_ms="<<parallel_tool_ms
            <<" generic_section_ms="<<section_ms
            <<" generic_section_edges="<<section_edges
            <<" bounds_ms="<<spatial_bounds_ms
            <<" possible_pairs="<<spatial_possible_pairs
            <<" candidate_pairs="<<spatial_candidates.size()
            <<" intersection_ms="<<spatial_section_ms
            <<" nonempty_pairs="<<spatial_nonempty_pairs
            <<" curves="<<spatial_section_curves
            <<" points="<<spatial_section_points
            <<" tangent_pairs="<<spatial_tangent_pairs
            <<" curves_with_both_pcurves="
            <<spatial_curves_with_both_pcurves
            <<" faces_without_reference="
            <<spatial_faces_without_reference
            <<" bounded_curves="<<spatial_bounded_curves
            <<" unique_curve_signatures="
            <<spatial_curve_signatures.size()
            <<" parented_curves="<<spatial_parented_curves
            <<" unique_parented_curve_signatures="
            <<spatial_parented_curve_signatures.size()
            <<" trim_ms="<<spatial_trim_ms
            <<" trimmed_intervals="<<spatial_trimmed_intervals
            <<" unique_trimmed_signatures="
            <<spatial_trimmed_signatures.size()
            <<" parented_trimmed_intervals="
            <<spatial_parented_trimmed_intervals
            <<" unique_parented_trimmed_signatures="
            <<spatial_parented_trimmed_signatures.size()
            <<" section_unique_signatures="
            <<section_signature_counts.size()
            <<" section_edges_without_signature="
            <<section_edges_without_signature
            <<" section_signatures_missing_from_trim="
            <<section_signatures_missing_from_trim
            <<" trimmed_signatures_missing_from_section="
            <<trimmed_signatures_missing_from_section
            <<" section_multiplicity_excess="
            <<section_multiplicity_excess
            <<" trimmed_multiplicity_excess="
            <<trimmed_multiplicity_excess
            <<" coverage_ms="<<coverage_ms
            <<" section_edges_covered_by_trim="
            <<section_edges_covered_by_trim
            <<" trimmed_intervals_covered_by_section="
            <<trimmed_intervals_covered_by_section
            <<" tangent_section_ms="<<tangent_section_ms
            <<" tangent_section_edges="<<tangent_section_edges
            <<" section_edges_covered_by_graph="
            <<section_edges_covered_by_graph
            <<" reusable_graph_ms="<<reusable_graph_ms
            <<" reusable_graph_curves="<<reusable_graph_curves
            <<" reusable_graph_transverse="
            <<reusable_graph_transverse
            <<" reusable_graph_coplanar="<<reusable_graph_coplanar
            <<" section_edges_covered_by_reusable_graph="
            <<section_edges_covered_by_reusable_graph
            <<" reusable_graph_curves_covered_by_section="
            <<reusable_graph_curves_covered_by_section
            <<" reusable_union_ms="<<reusable_union_ms
            <<" reusable_union_faces="<<reusable_union_faces.Extent()
            <<" reusable_union_edges="<<reusable_union_edges.Extent()
            <<" reusable_union_graph_ms="
            <<reusable_union.metrics.graph_ms
            <<" reusable_union_split_ms="
            <<reusable_union.metrics.split_ms
            <<" reusable_union_classify_ms="
            <<reusable_union.metrics.classify_ms
            <<" reusable_union_sew_ms="
            <<reusable_union.metrics.sew_ms
            <<" reusable_union_unify_ms="
            <<reusable_union.metrics.unify_ms
            <<" reusable_union_face_provenance="
            <<reusable_union.faces.size()
            <<" reusable_union_retained_patches="
            <<reusable_union.metrics.retained_patches
            <<" reusable_union_internal_patches="
            <<reusable_union.metrics.discarded_internal_patches
            <<" reusable_union_boundary_curves_skipped="
            <<reusable_union.metrics.boundary_curves_skipped
            <<" reusable_union_shells="
            <<reusable_union.metrics.sewed_shells
            <<" reusable_union_free_edges="
            <<reusable_union.metrics.free_edges
            <<" reusable_union_multiple_edges="
            <<reusable_union.metrics.multiple_edges
            <<" reusable_union_area="
            <<std::abs(reusable_union_surface.Mass())
            <<" reusable_union_volume="
            <<std::abs(reusable_union_volume.Mass())
            <<" local_split_ms="<<local_split_ms
            <<" local_split_faces="<<local_split_faces
            <<" local_split_patches="<<local_split_patches
            <<" face_boundary_curves_skipped="
            <<face_boundary_curves_skipped
            <<" all_local_patches="<<local_patches.size()
            <<" source_face_area="<<source_face_area
            <<" split_patch_area="<<split_patch_area
            <<" split_area_difference="
            <<std::abs(source_face_area-split_patch_area)
            <<" ray_load_ms="<<ray_load_ms
            <<" classify_ms="<<classify_ms
            <<" containment_queries="<<containment_queries
            <<" ray_classify_ms="<<ray_classify_ms
            <<" solid_classifier_ms="<<solid_classifier_ms
            <<" ray_fallbacks="<<ray_fallbacks
            <<" ray_mismatches="<<ray_mismatches
            <<" patch_sample_failures="<<patch_sample_failures
            <<" reversed_patch_normals="<<reversed_patch_normals
            <<" retained_union_patches="<<retained_union_patches
            <<" internal_union_patches="<<internal_union_patches
            <<" retained_union_area="<<retained_union_area
            <<" sew_ms="<<sew_ms
            <<" sew_free_edges="<<sewing.NbFreeEdges()
            <<" sew_multiple_edges="<<sewing.NbMultipleEdges()
            <<" sewed_shells="<<sewed_shells.Extent()
            <<" grouped_solid_oriented="<<grouped_solid_oriented
            <<" grouped_solid_valid="<<grouped_solid_valid
            <<" grouped_solid_topology_valid="
            <<grouped_solid_topology_valid
            <<" grouped_solid_area="
            <<std::abs(grouped_solid_surface.Mass())
            <<" grouped_solid_volume="
            <<std::abs(grouped_solid_volume.Mass())
            <<" graph_unify_ms="<<graph_unify_ms
            <<" sewed_faces_without_identity="
            <<sewed_faces_without_identity
            <<" identity_boundary_edges_kept="
            <<identity_boundary_edges_kept
            <<" same_reference_edges_merged="
            <<same_reference_edges_merged
            <<" coplanar_role_edges_merged="
            <<coplanar_role_edges_merged
            <<" unified_graph_valid="<<unified_graph_valid
            <<" unified_graph_faces="<<unified_graph_faces.Extent()
            <<" unified_graph_edges="<<unified_graph_edges.Extent()
            <<" unified_graph_shells="<<unified_graph_shells.Extent()
            <<" unified_graph_solids="<<unified_graph_solids.Extent()
            <<" unified_graph_area="
            <<std::abs(unified_graph_surface.Mass())
            <<" unified_graph_volume="
            <<std::abs(unified_graph_volume.Mass())
            <<" unified_faces_without_source="
            <<unified_faces_without_source
            <<" unified_faces_with_multiple_sources="
            <<unified_faces_with_multiple_sources
            <<" graph_face_aliases="<<graph_face_aliases.size()
            <<" expected_face_references="
            <<expected_graph_face_references.size()
            <<" expected_face_references_without_source_alias="
            <<expected_face_references_without_source_alias.size()
            <<" solid_fix_ms="<<solid_fix_ms
            <<" fixed_union_valid="<<fixed_union_valid
            <<" fixed_union_faces="<<fixed_union_faces.Extent()
            <<" fixed_union_edges="<<fixed_union_edges.Extent()
            <<" fixed_union_shells="<<fixed_union_shells.Extent()
            <<" fixed_union_solids="<<fixed_union_solids.Extent()
            <<" fixed_union_area="
            <<std::abs(fixed_union_surface.Mass())
            <<" fixed_union_volume="
            <<std::abs(fixed_union_volume.Mass())
            <<" oriented_solids="<<oriented_solids
            <<" classified_union_valid="<<classified_union_valid
            <<" classified_union_faces="<<classified_union_faces.Extent()
            <<" classified_union_edges="<<classified_union_edges.Extent()
            <<" classified_union_area="
            <<std::abs(classified_union_surface.Mass())
            <<" classified_union_volume="
            <<std::abs(classified_union_volume.Mass())
            <<" expected_union_area="<<expected_union_area
            <<" expected_union_volume="<<expected_union_volume
            <<" expected_union_shells="<<expected_probe_shells.Extent()
            <<" expected_union_solids="<<expected_probe_solids.Extent()
            <<" expected_union_faces="<<expected_probe_faces.Extent()
            <<" expected_union_edges="<<expected_probe_edges.Extent()
            <<" material_check_ms="<<material_check_ms
            <<" expected_minus_graph="<<expected_minus_graph
            <<" graph_minus_expected="<<graph_minus_expected
            <<" material_point_samples="<<material_point_samples
            <<" material_point_mismatches="<<material_point_mismatches
            <<" material_surface_samples="<<material_surface_samples
            <<" material_surface_mismatches="
            <<material_surface_mismatches
            <<" material_surface_sample_failures="
            <<material_surface_sample_failures
            <<" material_surface_edge_skips="
            <<material_surface_edge_skips
            <<" expected_surface_mismatches="
            <<expected_surface_mismatches
            <<" graph_surface_mismatches="<<graph_surface_mismatches
            <<" expected_self_orientation_errors="
            <<expected_self_orientation_errors
            <<" graph_self_orientation_errors="
            <<graph_self_orientation_errors
            <<" graph_orientation_error_references="
            <<graph_orientation_error_reference_text
            <<" union_area_difference="
            <<std::abs(retained_union_area-
                expected_union_area)<<'\n';
        return 0;
    }

    if(localized_treatment_only) {
        std::vector<std::size_t> treatment_indices;
        for(std::size_t index=last+1;index<operations.size();++index) {
            if(!std::holds_alternative<kernel::FilletRequest>(
                    operations[index].primitive))break;
            treatment_indices.push_back(index);
        }
        if(treatment_indices.empty())
            throw std::runtime_error("No downstream Fillet block");
        std::set<std::string> local_owners;
        for(const auto index:treatment_indices)
            for(const auto& edge:std::get<kernel::FilletRequest>(
                    operations[index].primitive).edges) {
                local_owners.insert(edge.owner_id);
                for(std::size_t source_index=first;source_index<=last;
                        ++source_index)
                    if(edge.semantic_key.find(
                           operations[source_index].owner_id)!=
                       std::string::npos)
                        local_owners.insert(
                            operations[source_index].owner_id);
            }
        struct LocalSourceTopology {
            BoundFaceShape topology;
            std::size_t history_index{};
        };
        std::vector<LocalSourceTopology> local_sources;
        TopTools_ListOfShape local_arguments,local_tools;
        bool first_local=true;
        for(const auto& owner:local_owners) {
            const auto source_tool=feature_tools.find(owner);
            if(source_tool==feature_tools.end())
                throw std::runtime_error(
                    "Localized Fillet source lies outside the Add batch");
            auto topology=bound_face_shape(source_tool->second);
            if(topology.root.IsNull())throw std::runtime_error(
                "Localized Fillet source has no native shape");
            const auto operation=std::ranges::find_if(operations,
                [&](const auto& candidate) {
                    return candidate.owner_id==owner;
                });
            if(operation==operations.end())
                throw std::runtime_error(
                    "Localized Fillet source has no history position");
            if(first_local) {
                local_arguments.Append(topology.root);first_local=false;
            } else local_tools.Append(topology.root);
            local_sources.push_back({std::move(topology),
                static_cast<std::size_t>(operation-operations.begin())});
        }
        if(first_local||local_tools.IsEmpty())
            throw std::runtime_error(
                "Localized Fillet needs at least two source tools");
        const auto localized_start=Clock::now();
        BRepAlgoAPI_BooleanOperation local_fuse;
        local_fuse.SetOperation(BOPAlgo_FUSE);
        local_fuse.SetArguments(local_arguments);
        local_fuse.SetTools(local_tools);
        local_fuse.SetRunParallel(true);
        local_fuse.SetFuzzyValue(tolerance);
        local_fuse.Build();
        const auto local_fuse_done=Clock::now();
        if(!local_fuse.IsDone()||local_fuse.Shape().IsNull())
            throw std::runtime_error("Localized source Fuse failed");
        ShapeUpgrade_UnifySameDomain local_source_unify(
            local_fuse.Shape(),true,true,false);
        local_source_unify.SetLinearTolerance(tolerance);
        local_source_unify.Build();
        const auto local_unify_done=Clock::now();
        TopoDS_Shape local_shape=local_source_unify.Shape();
        if(local_shape.IsNull()||!BRepCheck_Analyzer(local_shape).IsValid())
            throw std::runtime_error("Localized source Fuse is invalid");
        struct LocalEdgeAliases {
            TopoDS_Edge shape;
            std::vector<kernel::EdgeReference> references;
        };
        TopTools_IndexedMapOfShape local_faces,local_edges,local_vertices;
        TopExp::MapShapes(local_shape,TopAbs_FACE,local_faces);
        TopExp::MapShapes(local_shape,TopAbs_EDGE,local_edges);
        TopExp::MapShapes(local_shape,TopAbs_VERTEX,local_vertices);
        std::vector<std::vector<kernel::FaceReference>> local_face_sources(
            static_cast<std::size_t>(local_faces.Extent())+1);
        std::vector<std::set<std::string>> local_edge_face_supports(
            static_cast<std::size_t>(local_edges.Extent())+1);
        std::vector<std::set<std::string>> local_vertex_face_supports(
            static_cast<std::size_t>(local_vertices.Extent())+1);
        std::map<std::string,kernel::FaceReference> local_source_face_by_token;
        std::map<std::string,std::vector<TopoDS_Face>>
            local_source_face_shapes_by_token;
        std::vector<kernel::OcctBoundaryFace> local_resolution_source_faces;
        const auto append_unique_reference=[](auto& references,
                const auto& reference) {
            if(std::ranges::none_of(references,[&](const auto& candidate) {
                    return candidate==reference;
                }))references.push_back(reference);
        };
        const auto attach_after_unify=[&](const TopoDS_Shape& source,
                TopAbs_ShapeEnum kind,const auto& attach) {
            if(attach(source))return;
            bool changed=false;
            const auto history=local_source_unify.History();
            if(!history.IsNull())for(const auto* descendants:{
                    &history->Modified(source),&history->Generated(source)})
                for(TopTools_ListIteratorOfListOfShape iterator(*descendants);
                        iterator.More();iterator.Next())
                    if(iterator.Value().ShapeType()==kind)
                        changed=attach(iterator.Value())||changed;
            if(!changed&&(history.IsNull()||!history->IsRemoved(source)))
                static_cast<void>(attach(source));
        };
        const auto fuse_descendants=[&](const TopoDS_Shape& source,
                TopAbs_ShapeEnum kind) {
            std::vector<TopoDS_Shape> result;
            for(const auto* descendants:{&local_fuse.Modified(source),
                    &local_fuse.Generated(source)})
                for(TopTools_ListIteratorOfListOfShape iterator(*descendants);
                        iterator.More();iterator.Next())
                    if(iterator.Value().ShapeType()==kind)
                        result.push_back(iterator.Value());
            if(result.empty()&&!local_fuse.IsDeleted(source))
                result.push_back(source);
            return result;
        };
        for(const auto& source:local_sources) {
            for(const auto& [face_shape,reference]:source.topology.faces) {
                const auto token=encoded_reference(reference);
                local_source_face_by_token.insert_or_assign(token,reference);
                local_source_face_shapes_by_token[token].push_back(
                    TopoDS::Face(face_shape));
                local_resolution_source_faces.push_back({
                    TopoDS::Face(face_shape),{reference},
                    source.history_index});
                const auto descendants=fuse_descendants(
                    face_shape,TopAbs_FACE);
                for(const auto& descendant:descendants) {
                    attach_after_unify(descendant,TopAbs_FACE,
                        [&](const TopoDS_Shape& shape) {
                            const int index=local_faces.FindIndex(shape);
                            if(!index)return false;
                            append_unique_reference(local_face_sources[
                                static_cast<std::size_t>(index)],reference);
                            return true;
                        });
                    for(TopExp_Explorer edge(descendant,TopAbs_EDGE);
                            edge.More();edge.Next())
                        attach_after_unify(edge.Current(),TopAbs_EDGE,
                            [&](const TopoDS_Shape& shape) {
                                const int index=local_edges.FindIndex(shape);
                                if(!index)return false;
                                local_edge_face_supports[
                                    static_cast<std::size_t>(index)].insert(token);
                                return true;
                            });
                    for(TopExp_Explorer vertex(descendant,TopAbs_VERTEX);
                            vertex.More();vertex.Next())
                        attach_after_unify(vertex.Current(),TopAbs_VERTEX,
                            [&](const TopoDS_Shape& shape) {
                                const int index=local_vertices.FindIndex(shape);
                                if(!index)return false;
                                local_vertex_face_supports[
                                    static_cast<std::size_t>(index)].insert(token);
                                return true;
                            });
                }
            }
        }
        std::vector<std::optional<kernel::FaceReference>> canonical_local_faces(
            static_cast<std::size_t>(local_faces.Extent())+1);
        const auto reference_age=[&](const auto& reference) {
            const auto operation=std::ranges::find_if(operations,
                [&](const auto& candidate) {
                    return candidate.owner_id==reference.owner_id;
                });
            return operation==operations.end()?std::size_t{}:
                static_cast<std::size_t>(operation-operations.begin())+1;
        };
        for(int index=1;index<=local_faces.Extent();++index) {
            auto& sources=local_face_sources[static_cast<std::size_t>(index)];
            if(sources.size()==1)canonical_local_faces[
                static_cast<std::size_t>(index)]=sources.front();
            else if(!sources.empty()&&BRepAdaptor_Surface(
                    TopoDS::Face(local_faces(index))).GetType()==GeomAbs_Plane)
                canonical_local_faces[static_cast<std::size_t>(index)]=
                    *std::ranges::min_element(sources,{},reference_age);
        }
        TopTools_IndexedDataMapOfShapeListOfShape local_edge_faces,
            local_vertex_faces;
        TopExp::MapShapesAndAncestors(local_shape,TopAbs_EDGE,TopAbs_FACE,
            local_edge_faces);
        TopExp::MapShapesAndAncestors(local_shape,TopAbs_VERTEX,TopAbs_FACE,
            local_vertex_faces);
        const auto face_tokens_for=[&](
                const TopTools_IndexedDataMapOfShapeListOfShape& ancestors,
                const TopoDS_Shape& child)
                ->std::optional<std::set<std::string>> {
            const int child_index=ancestors.FindIndex(child);
            if(!child_index)return std::set<std::string>{};
            std::set<std::string> result;
            for(TopTools_ListIteratorOfListOfShape iterator(
                    ancestors.FindFromIndex(child_index));iterator.More();
                    iterator.Next()) {
                const int face_index=local_faces.FindIndex(iterator.Value());
                if(!face_index||!canonical_local_faces[
                        static_cast<std::size_t>(face_index)])return std::nullopt;
                result.insert(encoded_reference(*canonical_local_faces[
                    static_cast<std::size_t>(face_index)]));
            }
            return result;
        };
        std::vector<LocalEdgeAliases> current_edge_aliases;
        current_edge_aliases.reserve(static_cast<std::size_t>(
            local_edges.Extent()));
        for(int index=1;index<=local_edges.Extent();++index)
            current_edge_aliases.push_back(
                {TopoDS::Edge(local_edges(index)),{}});
        const auto attach_edge_alias=[&](const TopoDS_Shape& shape,
                const kernel::EdgeReference& reference) {
            const int index=local_edges.FindIndex(shape);
            if(!index)return false;
            append_unique_reference(current_edge_aliases[
                static_cast<std::size_t>(index-1)].references,reference);
            return true;
        };
        for(const auto& source:local_sources)
            for(const auto& [edge_shape,reference]:source.topology.edges)
                for(const auto& descendant:fuse_descendants(
                        edge_shape,TopAbs_EDGE))
                    attach_after_unify(descendant,TopAbs_EDGE,
                        [&](const TopoDS_Shape& shape) {
                            return attach_edge_alias(shape,reference);
                        });
        std::size_t reconstructed_aliases{},parent_incidence_aliases{};
        for(int index=1;index<=local_edges.Extent();++index) {
            auto adjacent=face_tokens_for(local_edge_faces,local_edges(index));
            if(local_edge_face_supports[static_cast<std::size_t>(index)].size()>=2)
                adjacent=local_edge_face_supports[
                    static_cast<std::size_t>(index)];
            if(!adjacent||adjacent->empty())continue;
            std::string owner;
            std::size_t newest{};
            for(const auto& token:*adjacent) {
                const auto found=local_source_face_by_token.find(token);
                if(found==local_source_face_by_token.end())continue;
                const auto age=reference_age(found->second);
                if(age>=newest) {newest=age;owner=found->second.owner_id;}
            }
            if(owner.empty())continue;
            std::set<std::string> endpoint_supports;
            bool complete=true;
            TopTools_IndexedMapOfShape endpoints;
            TopExp::MapShapes(local_edges(index),TopAbs_VERTEX,endpoints);
            for(int endpoint=1;endpoint<=endpoints.Extent();++endpoint) {
                std::optional<std::set<std::string>> support;
                const int vertex_index=local_vertices.FindIndex(
                    endpoints(endpoint));
                if(vertex_index&&!local_vertex_face_supports[
                        static_cast<std::size_t>(vertex_index)].empty())
                    support=local_vertex_face_supports[
                        static_cast<std::size_t>(vertex_index)];
                else support=face_tokens_for(local_vertex_faces,
                    endpoints(endpoint));
                if(!support) {complete=false;break;}
                if(!support->empty())endpoint_supports.insert(
                    encoded_reference_set(*support));
            }
            if(!complete)continue;
            const kernel::EdgeReference reference{owner,
                "boolean:add:intersection:between:"+
                    encoded_reference_set(*adjacent)+":ends:"+
                    encoded_reference_set(endpoint_supports),{}};
            auto& aliases=current_edge_aliases[
                static_cast<std::size_t>(index-1)].references;
            const auto before=aliases.size();
            append_unique_reference(aliases,reference);
            reconstructed_aliases+=aliases.size()!=before;
        }
        std::vector<double> fillet_milliseconds;
        for(const auto operation_index:treatment_indices) {
            const auto fillet_start=Clock::now();
            const auto& request=std::get<kernel::FilletRequest>(
                operations[operation_index].primitive);
            if(request.mode!=kernel::FilletRequest::Mode::Constant)
                throw std::runtime_error(
                    "Localized prototype currently measures constant Fillets");
            BRepFilletAPI_MakeFillet fillet(local_shape);
            TopTools_IndexedMapOfShape added;
            for(const auto& reference:request.edges) {
                std::vector<TopoDS_Edge> matches;
                for(const auto& candidate:current_edge_aliases)
                    if(std::ranges::find(candidate.references,reference)!=
                            candidate.references.end())
                        matches.push_back(candidate.shape);
                if(matches.empty()) {
                    const auto resolved=
                        kernel::occt_resolve_add_intersection_edges(
                            local_shape,local_resolution_source_faces,
                            std::span(&reference,1),tolerance);
                    matches.push_back(resolved.front().shape);
                    auto& aliases=std::ranges::find_if(
                        current_edge_aliases,[&](const auto& candidate) {
                            return candidate.shape.IsSame(matches.front());
                        })->references;
                    append_unique_reference(aliases,reference);
                    ++parent_incidence_aliases;
                }
                if(matches.empty()) {
                    const auto decode_set=[](std::string_view encoded) {
                        std::set<std::string> result;
                        std::size_t offset{};
                        while(offset<encoded.size()) {
                            const auto colon=encoded.find(':',offset);
                            if(colon==std::string_view::npos)break;
                            std::size_t length{};
                            const auto size=encoded.substr(offset,colon-offset);
                            const auto parsed=std::from_chars(size.data(),
                                size.data()+size.size(),length);
                            if(parsed.ec!=std::errc{}||
                               colon+1+length>encoded.size())break;
                            result.emplace(encoded.substr(colon+1,length));
                            offset=colon+1+length;
                        }
                        return result;
                    };
                    constexpr std::string_view prefix=
                        "boolean:add:intersection:between:";
                    const auto separator=reference.semantic_key.find(
                        ":ends:",prefix.size());
                    if(reference.semantic_key.starts_with(prefix)&&
                       separator!=std::string::npos) {
                        const auto parents=decode_set(std::string_view(
                            reference.semantic_key).substr(prefix.size(),
                                separator-prefix.size()));
                        const auto encoded_ends=decode_set(std::string_view(
                            reference.semantic_key).substr(separator+6));
                        std::vector<std::set<std::string>> endpoint_groups;
                        for(const auto& encoded:encoded_ends)
                            endpoint_groups.push_back(decode_set(encoded));
                        const auto point_on_reference=[&](const gp_Pnt& point,
                                const std::string& token) {
                            const auto found=
                                local_source_face_shapes_by_token.find(token);
                            if(found==local_source_face_shapes_by_token.end())
                                return false;
                            const auto probe=BRepBuilderAPI_MakeVertex(
                                point).Vertex();
                            return std::ranges::any_of(found->second,
                                [&](const TopoDS_Face& face) {
                                    BRepExtrema_DistShapeShape distance(
                                        probe,face);
                                    return distance.IsDone()&&
                                        distance.NbSolution()>0&&
                                        distance.Value()<=
                                            std::max(1.0e-7,2.*tolerance);
                                });
                        };
                        const auto edge_on_reference=[&](
                                const TopoDS_Edge& edge,
                                const std::string& token) {
                            BRepAdaptor_Curve curve(edge);
                            const double first=curve.FirstParameter();
                            const double last=curve.LastParameter();
                            if(!std::isfinite(first)||!std::isfinite(last))
                                return false;
                            for(int sample=0;sample<9;++sample)
                                if(!point_on_reference(curve.Value(first+
                                        (last-first)*sample/8.),token))
                                    return false;
                            return true;
                        };
                        for(const auto& candidate:current_edge_aliases) {
                            if(!std::ranges::all_of(parents,
                                    [&](const auto& token) {
                                        return edge_on_reference(
                                            candidate.shape,token);
                                    }))continue;
                            if(!endpoint_groups.empty()) {
                                TopTools_IndexedMapOfShape endpoints;
                                TopExp::MapShapes(candidate.shape,
                                    TopAbs_VERTEX,endpoints);
                                bool complete=true;
                                for(const auto& group:endpoint_groups) {
                                    bool supported=false;
                                    for(int endpoint=1;
                                            endpoint<=endpoints.Extent();
                                            ++endpoint) {
                                        const auto point=BRep_Tool::Pnt(
                                            TopoDS::Vertex(
                                                endpoints(endpoint)));
                                        if(std::ranges::all_of(group,
                                                [&](const auto& token) {
                                                    return point_on_reference(
                                                        point,token);
                                                })) {
                                            supported=true;break;
                                        }
                                    }
                                    if(!supported) {complete=false;break;}
                                }
                                if(!complete)continue;
                            }
                            matches.push_back(candidate.shape);
                        }
                        if(matches.size()==1) {
                            auto& aliases=std::ranges::find_if(
                                current_edge_aliases,[&](const auto& candidate) {
                                    return candidate.shape.IsSame(matches.front());
                                })->references;
                            append_unique_reference(aliases,reference);
                            ++parent_incidence_aliases;
                        }
                    }
                }
                if(matches.empty()) {
                    std::cerr<<"localized structural edge missing owner="
                        <<reference.owner_id<<" key="
                        <<reference.semantic_key<<'\n';
                    const auto requested_separator=
                        reference.semantic_key.find(":ends:");
                    const auto requested_between=reference.semantic_key.substr(
                        0,requested_separator);
                    const auto requested_ends=requested_separator==
                            std::string::npos?std::string{}:
                        reference.semantic_key.substr(requested_separator);
                    std::size_t reported{},same_between{},same_ends{};
                    for(const auto& candidate:current_edge_aliases)
                        for(const auto& alias:candidate.references)
                            if(alias.owner_id==reference.owner_id) {
                                const auto separator=alias.semantic_key.find(
                                    ":ends:");
                                const bool between=alias.semantic_key.substr(
                                    0,separator)==requested_between;
                                const bool ends=separator!=std::string::npos&&
                                    alias.semantic_key.substr(separator)==
                                        requested_ends;
                                same_between+=between;same_ends+=ends;
                                if((between||ends)&&reported++<4)
                                    std::cerr
                                        <<"localized structural near candidate between="
                                        <<between<<" ends="<<ends<<" key="
                                        <<alias.semantic_key<<'\n';
                            }
                    std::cerr<<"localized structural near counts between="
                        <<same_between<<" ends="<<same_ends<<'\n';
                    throw std::runtime_error(
                        "Localized Fillet reference has no structural descendant");
                }
                for(const auto& edge:matches) {
                    if(added.Contains(edge))continue;
                    added.Add(edge);
                    if(fillet.Contour(edge)==0)
                        fillet.Add(request.radius_start,edge);
                }
            }
            fillet.SetParams(1.0e-2,
                std::max(1.0e-7,
                    operations[operation_index].boolean_tolerance),
                1.0e-5,
                std::max(1.0e-7,
                    operations[operation_index].boolean_tolerance),
                1.0e-5,1.0e-3);
            fillet.Build();
            if(!fillet.IsDone()||fillet.Shape().IsNull())
                throw std::runtime_error("Localized Fillet build failed");
            ShapeUpgrade_UnifySameDomain fillet_unify(
                fillet.Shape(),true,true,false);
            fillet_unify.SetLinearTolerance(std::max(1.0e-7,
                operations[operation_index].boolean_tolerance));
            fillet_unify.Build();
            const auto next_local_shape=fillet_unify.Shape();
            TopTools_IndexedMapOfShape next_edges;
            TopExp::MapShapes(next_local_shape,TopAbs_EDGE,next_edges);
            std::vector<LocalEdgeAliases> next_aliases;
            next_aliases.reserve(static_cast<std::size_t>(next_edges.Extent()));
            for(int index=1;index<=next_edges.Extent();++index)
                next_aliases.push_back({TopoDS::Edge(next_edges(index)),{}});
            const auto attach_next=[&](const TopoDS_Shape& shape,
                    const auto& references) {
                const int index=next_edges.FindIndex(shape);
                if(!index)return false;
                for(const auto& reference:references)
                    append_unique_reference(next_aliases[
                        static_cast<std::size_t>(index-1)].references,reference);
                return true;
            };
            const auto treatment_unify_history=fillet_unify.History();
            for(const auto& source:current_edge_aliases) {
                std::vector<TopoDS_Shape> descendants;
                for(const auto* values:{&fillet.Modified(source.shape),
                        &fillet.Generated(source.shape)})
                    for(TopTools_ListIteratorOfListOfShape iterator(*values);
                            iterator.More();iterator.Next())
                        if(iterator.Value().ShapeType()==TopAbs_EDGE)
                            descendants.push_back(iterator.Value());
                if(descendants.empty()&&!fillet.IsDeleted(source.shape))
                    descendants.push_back(source.shape);
                for(const auto& descendant:descendants) {
                    bool changed=false;
                    if(!treatment_unify_history.IsNull())
                        for(const auto* values:{
                                &treatment_unify_history->Modified(descendant),
                                &treatment_unify_history->Generated(descendant)})
                            for(TopTools_ListIteratorOfListOfShape iterator(*values);
                                    iterator.More();iterator.Next())
                                if(iterator.Value().ShapeType()==TopAbs_EDGE)
                                    changed=attach_next(iterator.Value(),
                                        source.references)||changed;
                    if(!changed&&(treatment_unify_history.IsNull()||
                            !treatment_unify_history->IsRemoved(descendant)))
                        static_cast<void>(attach_next(descendant,
                            source.references));
                }
            }
            local_shape=next_local_shape;
            current_edge_aliases=std::move(next_aliases);
            if(local_shape.IsNull()||
               !BRepCheck_Analyzer(local_shape).IsValid())
                throw std::runtime_error(
                    "Localized Fillet result is invalid");
            fillet_milliseconds.push_back(
                std::chrono::duration<double,std::milli>(
                    Clock::now()-fillet_start).count());
        }
        const auto treatments_done=Clock::now();
        TopTools_ListOfShape final_arguments,final_tools;
        final_arguments.Append(input_shape.root);
        for(std::size_t index=first;index<=last;++index) {
            if(local_owners.contains(operations[index].owner_id))continue;
            const auto source_tool=feature_tools.find(
                operations[index].owner_id);
            if(source_tool==feature_tools.end())continue;
            const auto shape=bound_face_shape(source_tool->second).root;
            if(!shape.IsNull())final_tools.Append(shape);
        }
        final_tools.Append(local_shape);
        BRepAlgoAPI_BooleanOperation localized_fuse;
        localized_fuse.SetOperation(BOPAlgo_FUSE);
        localized_fuse.SetArguments(final_arguments);
        localized_fuse.SetTools(final_tools);
        localized_fuse.SetRunParallel(true);
        localized_fuse.SetFuzzyValue(tolerance);
        localized_fuse.Build();
        const auto final_fuse_done=Clock::now();
        if(!localized_fuse.IsDone()||localized_fuse.Shape().IsNull())
            throw std::runtime_error("Localized final Fuse failed");
        ShapeUpgrade_UnifySameDomain localized_unify(
            localized_fuse.Shape(),true,true,false);
        localized_unify.SetLinearTolerance(tolerance);
        localized_unify.Build();
        const auto final_unify_done=Clock::now();
        const auto localized_shape=localized_unify.Shape();
        if(localized_shape.IsNull()||
           !BRepCheck_Analyzer(localized_shape).IsValid())
            throw std::runtime_error(
                "Localized final result is invalid");
        const auto difference_volume=[&](const TopoDS_Shape& minuend,
                const TopoDS_Shape& subtrahend) {
            BRepAlgoAPI_Cut cut(minuend,subtrahend);
            cut.SetRunParallel(true);
            cut.SetFuzzyValue(tolerance);
            cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
            GProp_GProps properties;
            BRepGProp::VolumeProperties(cut.Shape(),properties);
            return std::abs(properties.Mass());
        };
        const auto expected=kernel::read_calculated_body_shape(
            local_body(treatment_indices.back()));
        const auto verification_start=Clock::now();
        const double expected_minus_localized=
            difference_volume(expected,localized_shape);
        const double localized_minus_expected=
            difference_volume(localized_shape,expected);
        TopTools_IndexedMapOfShape localized_faces,expected_faces;
        TopExp::MapShapes(localized_shape,TopAbs_FACE,localized_faces);
        TopExp::MapShapes(expected,TopAbs_FACE,expected_faces);
        const auto elapsed=std::chrono::duration<double,std::milli>(
            Clock::now()-localized_start).count();
        std::cout<<std::setprecision(17)
            <<"localized_treatment_probe first="<<first
            <<" last="<<last
            <<" source_owners="<<local_owners.size()
            <<" treatments="<<treatment_indices.size()
            <<" reconstructed_aliases="<<reconstructed_aliases
            <<" parent_incidence_aliases="<<parent_incidence_aliases
            <<" elapsed_ms="<<elapsed
            <<" local_fuse_ms="
            <<std::chrono::duration<double,std::milli>(
                local_fuse_done-localized_start).count()
            <<" local_unify_ms="
            <<std::chrono::duration<double,std::milli>(
                local_unify_done-local_fuse_done).count()
            <<" fillet_ms=";
        for(std::size_t index=0;index<fillet_milliseconds.size();++index) {
            if(index)std::cout<<',';
            std::cout<<fillet_milliseconds[index];
        }
        std::cout
            <<" treatment_total_ms="
            <<std::chrono::duration<double,std::milli>(
                treatments_done-local_unify_done).count()
            <<" final_fuse_ms="
            <<std::chrono::duration<double,std::milli>(
                final_fuse_done-treatments_done).count()
            <<" final_unify_ms="
            <<std::chrono::duration<double,std::milli>(
                final_unify_done-final_fuse_done).count()
            <<" verification_ms="
            <<std::chrono::duration<double,std::milli>(
                Clock::now()-verification_start).count()
            <<" expected_minus_localized="
            <<expected_minus_localized
            <<" localized_minus_expected="
            <<localized_minus_expected
            <<" localized_faces="<<localized_faces.Extent()
            <<" expected_faces="<<expected_faces.Extent()<<'\n';
        return 0;
    }

    TopTools_ListOfShape arguments,tool_list;
    arguments.Append(input_shape.root);
    for(const auto& tool:tools)tool_list.Append(tool);
    BRepAlgoAPI_BooleanOperation fuse;
    fuse.SetOperation(boolean_operation==kernel::BooleanOperation::Add
        ?BOPAlgo_FUSE:BOPAlgo_CUT);
    fuse.SetArguments(arguments);fuse.SetTools(tool_list);
    fuse.SetRunParallel(true);fuse.SetToFillHistory(true);
    fuse.SetFuzzyValue(tolerance);
    const auto fuse_start=Clock::now();fuse.Build();
    const auto fuse_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-fuse_start).count();
    if(!fuse.IsDone()||fuse.Shape().IsNull()||
       !BRepCheck_Analyzer(fuse.Shape()).IsValid())
        throw std::runtime_error("Batch Add did not produce a valid shape");
    TopTools_IndexedMapOfShape raw_batch_edge_map;
    TopExp::MapShapes(fuse.Shape(),TopAbs_EDGE,raw_batch_edge_map);
    TopTools_IndexedMapOfShape raw_batch_face_map;
    TopExp::MapShapes(fuse.Shape(),TopAbs_FACE,raw_batch_face_map);
    std::vector<std::set<std::string>> raw_batch_face_ancestry(
        static_cast<std::size_t>(raw_batch_face_map.Extent())+1);
    const auto attach_raw_face=[&](const TopoDS_Shape& face,
            const kernel::FaceReference& reference) {
        const int index=raw_batch_face_map.FindIndex(face);
        if(index)raw_batch_face_ancestry[static_cast<std::size_t>(index)]
            .insert(reference_key(reference));
    };
    for(const auto& [face,reference]:source_faces) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(face),&fuse.Generated(face)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())if(it.Value().ShapeType()==TopAbs_FACE) {
                attach_raw_face(it.Value(),reference);changed=true;
            }
        if(!changed&&!fuse.IsDeleted(face))attach_raw_face(face,reference);
    }
    std::vector<std::set<std::string>> raw_batch_edge_ancestry(
        static_cast<std::size_t>(raw_batch_edge_map.Extent())+1);
    const auto attach_raw_edge=[&](const TopoDS_Shape& edge,
            const kernel::EdgeReference& reference) {
        const int index=raw_batch_edge_map.FindIndex(edge);
        if(index)raw_batch_edge_ancestry[static_cast<std::size_t>(index)]
            .insert(reference_key(reference));
    };
    for(const auto& [edge,reference]:source_edges) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(edge),&fuse.Generated(edge)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())if(it.Value().ShapeType()==TopAbs_EDGE) {
                attach_raw_edge(it.Value(),reference);changed=true;
            }
        if(!changed&&!fuse.IsDeleted(edge))attach_raw_edge(edge,reference);
    }
    TopTools_IndexedMapOfShape raw_batch_vertex_map;
    TopExp::MapShapes(fuse.Shape(),TopAbs_VERTEX,raw_batch_vertex_map);
    std::vector<std::set<std::string>> raw_batch_vertex_ancestry(
        static_cast<std::size_t>(raw_batch_vertex_map.Extent())+1);
    const auto attach_raw_vertex=[&](const TopoDS_Shape& vertex,
            const kernel::VertexReference& reference) {
        const int index=raw_batch_vertex_map.FindIndex(vertex);
        if(index)raw_batch_vertex_ancestry[static_cast<std::size_t>(index)]
            .insert(reference_key(reference));
    };
    for(const auto& [vertex,reference]:source_vertices) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(vertex),
                &fuse.Generated(vertex)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())if(it.Value().ShapeType()==TopAbs_VERTEX) {
                attach_raw_vertex(it.Value(),reference);changed=true;
            }
        if(!changed&&!fuse.IsDeleted(vertex))
            attach_raw_vertex(vertex,reference);
    }
    const auto unify_start=Clock::now();
    ShapeUpgrade_UnifySameDomain unify(fuse.Shape(),true,true,false);
    unify.SetLinearTolerance(tolerance);unify.Build();
    const auto unified=unify.Shape();
    const auto unify_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-unify_start).count();
    if(unified.IsNull()||!BRepCheck_Analyzer(unified).IsValid())
        throw std::runtime_error("Batch Add unification did not produce a valid shape");
    const auto archive_start=Clock::now();
    BRepTools_ShapeSet batch_archive(false,false);
    batch_archive.SetFormatNb(TopTools_FormatVersion_CURRENT);
    batch_archive.Add(unified);
    std::ostringstream batch_stream;
    batch_archive.Write(batch_stream);batch_archive.Write(unified,batch_stream);
    kernel::StepRequest batch_request;
    batch_request.frozen_brep=std::make_shared<const std::string>(batch_stream.str());
    batch_request.reference_owner_id="batch-publish";
    std::vector<kernel::StepRequest::TopologyIdentity> batch_topology;
    const auto append_batch_topology=[&](TopAbs_ShapeEnum kind,
            kernel::StepRequest::TopologyIdentity::Kind identity_kind,
            std::string_view prefix) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(unified,kind,members);
        for(int index=1;index<=members.Extent();++index) {
            std::ostringstream locator;batch_archive.Write(members(index),locator);
            batch_topology.push_back({identity_kind,
                std::string(prefix)+std::to_string(index),
                "brep-ref-v1:"+locator.str()});
        }
    };
    using TopologyKind=kernel::StepRequest::TopologyIdentity::Kind;
    append_batch_topology(TopAbs_FACE,TopologyKind::Face,"batch:face:");
    append_batch_topology(TopAbs_EDGE,TopologyKind::Edge,"batch:edge:");
    append_batch_topology(TopAbs_VERTEX,TopologyKind::Vertex,"batch:vertex:");
    batch_request.topology=std::move(batch_topology);
    const auto archive_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-archive_start).count();
    kernel::HistoryOperation publish_operation{
        "batch-publish",std::move(batch_request),kernel::BooleanOperation::Add};
    publish_operation.boolean_tolerance=tolerance;
    publish_operation.mesh_deflection=operations[last].mesh_deflection;
    const auto publish_start=Clock::now();
    const auto published=kernel.evaluate_history({publish_operation});
    const auto publish_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-publish_start).count();
    if(published.size()!=1||!published.front().has_solid())
        throw std::runtime_error("Batch publication did not produce a solid packet");
    const auto expected=kernel::read_calculated_body_shape(local_body(last));
    GProp_GProps expected_properties,batch_properties;
    BRepGProp::VolumeProperties(expected,expected_properties);
    BRepGProp::VolumeProperties(unified,batch_properties);
    GProp_GProps expected_surface,batch_surface;
    BRepGProp::SurfaceProperties(expected,expected_surface);
    BRepGProp::SurfaceProperties(unified,batch_surface);
    const auto difference_volume=[&](const TopoDS_Shape& minuend,
            const TopoDS_Shape& subtrahend) {
        BRepAlgoAPI_Cut cut(minuend,subtrahend);
        cut.SetRunParallel(true);cut.SetFuzzyValue(tolerance);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull()||
           !BRepCheck_Analyzer(cut.Shape()).IsValid())
            throw std::runtime_error("Batch Add equivalence cut failed");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto expected_minus_batch=verify_equivalence
        ?difference_volume(expected,unified):-1.;
    const auto batch_minus_expected=verify_equivalence
        ?difference_volume(unified,expected):-1.;
    const auto solids=[](const TopoDS_Shape& shape) {
        std::size_t count{};
        for(TopExp_Explorer explorer(shape,TopAbs_SOLID);explorer.More();explorer.Next())++count;
        return count;
    };
    TopTools_IndexedMapOfShape batch_face_map;
    TopExp::MapShapes(unified,TopAbs_FACE,batch_face_map);
    std::vector<std::set<std::string>> batch_face_ancestry(
        static_cast<std::size_t>(batch_face_map.Extent())+1);
    std::vector<std::vector<kernel::FaceReference>> batch_face_sources(
        static_cast<std::size_t>(batch_face_map.Extent())+1);
    std::set<std::string> propagated_face_references;
    const auto attach_reference=[&](const TopoDS_Shape& shape,
            const kernel::FaceReference& reference) {
        const int index=batch_face_map.FindIndex(shape);
        if(index==0)return false;
        const auto key=reference_key(reference);
        propagated_face_references.insert(key);
        batch_face_ancestry[static_cast<std::size_t>(index)].insert(key);
        auto& sources=batch_face_sources[static_cast<std::size_t>(index)];
        if(std::ranges::none_of(sources,[&](const auto& current) {
                return reference_key(current)==key;
            }))sources.push_back(reference);
        return true;
    };
    const auto append_unified=[&](const TopoDS_Shape& shape,
            const kernel::FaceReference& reference) {
        bool changed=false;
        const auto history=unify.History();
        if(!history.IsNull())for(const auto* descendants:{
                &history->Modified(shape),&history->Generated(shape)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE) {
                    changed=attach_reference(it.Value(),reference)||changed;
                }
        if(!changed&&(history.IsNull()||!history->IsRemoved(shape)))
            static_cast<void>(attach_reference(shape,reference));
    };
    for(const auto& [face,reference]:source_faces) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(face),&fuse.Generated(face)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE) {
                    append_unified(it.Value(),reference);
                    changed=true;
                }
        if(!changed&&!fuse.IsDeleted(face))append_unified(face,reference);
    }
    TopTools_IndexedMapOfShape batch_edge_map;
    TopExp::MapShapes(unified,TopAbs_EDGE,batch_edge_map);
    TopTools_IndexedMapOfShape batch_vertex_map_for_supports;
    TopExp::MapShapes(unified,TopAbs_VERTEX,batch_vertex_map_for_supports);
    std::vector<std::set<std::string>> batch_edge_source_face_supports(
        static_cast<std::size_t>(batch_edge_map.Extent())+1);
    std::vector<std::set<std::string>> batch_vertex_source_face_supports(
        static_cast<std::size_t>(batch_vertex_map_for_supports.Extent())+1);
    std::map<std::string,kernel::FaceReference> source_face_references;
    const auto attach_unified_support=[&](const TopoDS_Shape& source,
            const std::string& token,TopAbs_ShapeEnum kind) {
        const auto attach=[&](const TopoDS_Shape& shape) {
            if(kind==TopAbs_EDGE) {
                const int index=batch_edge_map.FindIndex(shape);
                if(index)batch_edge_source_face_supports[
                    static_cast<std::size_t>(index)].insert(token);
                return index!=0;
            }
            const int index=batch_vertex_map_for_supports.FindIndex(shape);
            if(index)batch_vertex_source_face_supports[
                static_cast<std::size_t>(index)].insert(token);
            return index!=0;
        };
        bool changed=false;
        const auto history=unify.History();
        if(!history.IsNull())for(const auto* descendants:{
                &history->Modified(source),&history->Generated(source)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())
                if(it.Value().ShapeType()==kind)
                    changed=attach(it.Value())||changed;
        if(!changed&&(history.IsNull()||!history->IsRemoved(source)))
            static_cast<void>(attach(source));
    };
    // Record the exact source-face pair at the Boolean boundary, before
    // same-domain unification can erase a result-face distinction. This is the
    // research equivalent of carrying section-curve ancestry beside the B-Rep:
    // OCCT still owns the intersection geometry, while ZIMA owns its identity.
    for(const auto& [face,reference]:source_faces) {
        const auto token=encoded_reference(reference);
        source_face_references.insert_or_assign(token,reference);
        std::vector<TopoDS_Shape> descendants;
        for(const auto* generated:{&fuse.Modified(face),&fuse.Generated(face)})
            for(TopTools_ListIteratorOfListOfShape it(*generated);it.More();
                    it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE)
                    descendants.push_back(it.Value());
        if(descendants.empty()&&!fuse.IsDeleted(face))descendants.push_back(face);
        for(const auto& descendant:descendants) {
            for(TopExp_Explorer edge(descendant,TopAbs_EDGE);edge.More();edge.Next())
                attach_unified_support(edge.Current(),token,TopAbs_EDGE);
            for(TopExp_Explorer vertex(descendant,TopAbs_VERTEX);vertex.More();
                    vertex.Next())
                attach_unified_support(vertex.Current(),token,TopAbs_VERTEX);
        }
    }
    std::vector<std::set<std::string>> batch_edge_ancestry(
        static_cast<std::size_t>(batch_edge_map.Extent())+1);
    std::vector<std::vector<kernel::EdgeReference>> batch_edge_sources(
        static_cast<std::size_t>(batch_edge_map.Extent())+1);
    const auto attach_edge_reference=[&](const TopoDS_Shape& shape,
            const kernel::EdgeReference& reference) {
        const int index=batch_edge_map.FindIndex(shape);
        if(index==0)return false;
        const auto key=reference_key(reference);
        batch_edge_ancestry[static_cast<std::size_t>(index)].insert(key);
        auto& sources=batch_edge_sources[static_cast<std::size_t>(index)];
        if(std::ranges::none_of(sources,[&](const auto& current) {
                return reference_key(current)==key;
            }))sources.push_back(reference);
        return true;
    };
    const auto append_unified_edge=[&](const TopoDS_Shape& shape,
            const kernel::EdgeReference& reference) {
        bool changed=false;
        const auto history=unify.History();
        if(!history.IsNull())for(const auto* descendants:{
                &history->Modified(shape),&history->Generated(shape)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_EDGE)
                    changed=attach_edge_reference(it.Value(),reference)||changed;
        if(!changed&&(history.IsNull()||!history->IsRemoved(shape)))
            static_cast<void>(attach_edge_reference(shape,reference));
    };
    for(const auto& [edge,reference]:source_edges) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(edge),&fuse.Generated(edge)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_EDGE) {
                    append_unified_edge(it.Value(),reference);changed=true;
                }
        if(!changed&&!fuse.IsDeleted(edge))append_unified_edge(edge,reference);
    }
    std::vector<std::set<std::string>> batch_vertex_ancestry(
        static_cast<std::size_t>(batch_vertex_map_for_supports.Extent())+1);
    const auto attach_vertex_reference=[&](const TopoDS_Shape& shape,
            const kernel::VertexReference& reference) {
        const int index=batch_vertex_map_for_supports.FindIndex(shape);
        if(!index)return false;
        batch_vertex_ancestry[static_cast<std::size_t>(index)].insert(
            reference_key(reference));
        return true;
    };
    const auto append_unified_vertex=[&](const TopoDS_Shape& shape,
            const kernel::VertexReference& reference) {
        bool changed=false;
        const auto history=unify.History();
        if(!history.IsNull())for(const auto* descendants:{
                &history->Modified(shape),&history->Generated(shape)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())if(it.Value().ShapeType()==TopAbs_VERTEX)
                changed=attach_vertex_reference(it.Value(),reference)||changed;
        if(!changed&&(history.IsNull()||!history->IsRemoved(shape)))
            static_cast<void>(attach_vertex_reference(shape,reference));
    };
    for(const auto& [vertex,reference]:source_vertices) {
        bool changed=false;
        for(const auto* descendants:{&fuse.Modified(vertex),
                &fuse.Generated(vertex)})
            for(TopTools_ListIteratorOfListOfShape it(*descendants);it.More();
                    it.Next())if(it.Value().ShapeType()==TopAbs_VERTEX) {
                append_unified_vertex(it.Value(),reference);changed=true;
            }
        if(!changed&&!fuse.IsDeleted(vertex))
            append_unified_vertex(vertex,reference);
    }
    std::map<std::string,std::size_t> operation_age;
    for(std::size_t index=0;index<operations.size();++index)
        operation_age.emplace(operations[index].owner_id,index);
    const auto age=[&](const auto& reference) {
        const auto found=operation_age.find(reference.owner_id);
        return found==operation_age.end()?std::size_t{}:found->second+1;
    };
    std::vector<std::optional<kernel::FaceReference>> canonical_batch_faces(
        batch_face_sources.size());
    std::size_t unresolved_batch_faces{};
    for(std::size_t index=1;index<batch_face_sources.size();++index) {
        const auto& sources=batch_face_sources[index];
        if(sources.empty()) {
            ++unresolved_batch_faces;
            continue;
        }
        if(sources.size()==1) {
            canonical_batch_faces[index]=sources.front();
            continue;
        }
        // Match the permanent oldest-coplanar-face contract. Non-planar
        // multi-parent faces need the ordinary derived-face completion path
        // and stay deliberately unresolved in this research probe.
        const auto face=TopoDS::Face(batch_face_map(static_cast<int>(index)));
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) {
            ++unresolved_batch_faces;
            continue;
        }
        canonical_batch_faces[index]=*std::ranges::min_element(sources,
            {},age);
    }
    TopTools_IndexedDataMapOfShapeListOfShape batch_edge_faces,batch_vertex_faces;
    TopExp::MapShapesAndAncestors(
        unified,TopAbs_EDGE,TopAbs_FACE,batch_edge_faces);
    TopExp::MapShapesAndAncestors(
        unified,TopAbs_VERTEX,TopAbs_FACE,batch_vertex_faces);
    const auto face_tokens_for=[&](
            const TopTools_IndexedDataMapOfShapeListOfShape& ancestors,
            const TopoDS_Shape& child) -> std::optional<std::set<std::string>> {
        const int child_index=ancestors.FindIndex(child);
        if(child_index==0)return std::set<std::string>{};
        std::set<std::string> result;
        for(TopTools_ListIteratorOfListOfShape iterator(
                ancestors.FindFromIndex(child_index));iterator.More();
                iterator.Next()) {
            const int face_index=batch_face_map.FindIndex(iterator.Value());
            if(face_index==0||
               !canonical_batch_faces[static_cast<std::size_t>(face_index)])
                return std::nullopt;
            result.insert(encoded_reference(
                *canonical_batch_faces[static_cast<std::size_t>(face_index)]));
        }
        return result;
    };
    std::map<std::string,std::vector<int>> reconstructed_batch_edge_targets;
    std::vector<kernel::EdgeReference> reconstructed_batch_edge_references;
    std::vector<std::optional<kernel::EdgeReference>> reconstructed_batch_edge_by_index(
        static_cast<std::size_t>(batch_edge_map.Extent())+1);
    std::size_t reconstructable_intersection_edges{};
    for(int index=1;index<=batch_edge_map.Extent();++index) {
        const auto& edge=batch_edge_map(index);
        auto adjacent=face_tokens_for(batch_edge_faces,edge);
        if(batch_edge_source_face_supports[static_cast<std::size_t>(index)].size()>=2)
            adjacent=batch_edge_source_face_supports[
                static_cast<std::size_t>(index)];
        if(!adjacent||adjacent->empty())continue;
        std::string owner;
        std::size_t owner_age{};
        for(const auto& token:*adjacent) {
            const auto found=source_face_references.find(token);
            if(found==source_face_references.end())continue;
            const auto& reference=found->second;
            if(const auto current=age(reference);current>=owner_age) {
                owner_age=current;owner=reference.owner_id;
            }
        }
        if(owner.empty())continue;
        std::set<std::string> endpoint_supports;
        bool endpoints_complete=true;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        for(int vertex=1;vertex<=vertices.Extent();++vertex) {
            std::optional<std::set<std::string>> support;
            const int vertex_index=batch_vertex_map_for_supports.FindIndex(
                vertices(vertex));
            if(vertex_index&& !batch_vertex_source_face_supports[
                    static_cast<std::size_t>(vertex_index)].empty())
                support=batch_vertex_source_face_supports[
                    static_cast<std::size_t>(vertex_index)];
            else support=face_tokens_for(batch_vertex_faces,vertices(vertex));
            if(!support) {endpoints_complete=false;break;}
            if(!support->empty())
                endpoint_supports.insert(encoded_reference_set(*support));
        }
        if(!endpoints_complete)continue;
        kernel::EdgeReference reference{owner,
            "boolean:add:intersection:between:"+
                encoded_reference_set(*adjacent)+":ends:"+
                encoded_reference_set(endpoint_supports),{}};
        const auto key=reference_key(reference);
        reconstructed_batch_edge_targets[key].push_back(index);
        reconstructed_batch_edge_by_index[static_cast<std::size_t>(index)]=reference;
        reconstructed_batch_edge_references.push_back(std::move(reference));
        ++reconstructable_intersection_edges;
    }
    std::set<std::string> downstream_treatment_edges;
    std::map<std::string,kernel::EdgeReference> downstream_edge_references;
    std::size_t downstream_treatment_last=last;
    for(std::size_t index=last+1;index<operations.size();++index) {
        if(const auto* fillet=std::get_if<kernel::FilletRequest>(
                &operations[index].primitive)) {
            downstream_treatment_last=index;
            for(const auto& edge:fillet->edges) {
                downstream_treatment_edges.insert(reference_key(edge));
                downstream_edge_references.emplace(reference_key(edge),edge);
            }
        } else if(const auto* chamfer=std::get_if<kernel::ChamferRequest>(
                &operations[index].primitive)) {
            downstream_treatment_last=index;
            for(const auto& edge:chamfer->edges) {
                downstream_treatment_edges.insert(reference_key(edge));
                downstream_edge_references.emplace(reference_key(edge),edge);
            }
        } else break;
    }
    std::size_t downstream_treatment_missing{},downstream_treatment_fragments{};
    std::size_t downstream_treatment_reconstructed{};
    std::size_t downstream_between_matched{},downstream_ends_matched{};
    std::map<std::string,std::size_t> missing_by_owner;
    std::set<std::string> unresolved_downstream_edges;
    std::map<std::string,std::vector<int>> downstream_batch_edge_targets;
    for(const auto& requested:downstream_treatment_edges) {
        std::vector<int> matches;
        for(std::size_t index=1;index<batch_edge_ancestry.size();++index)
            if(batch_edge_ancestry[index].contains(requested))
                matches.push_back(static_cast<int>(index));
        if(!matches.empty())downstream_batch_edge_targets.emplace(
            requested,matches);
        downstream_treatment_fragments+=matches.size();
        if(matches.empty()) {
            const auto reconstructed=
                reconstructed_batch_edge_targets.find(requested);
            if(reconstructed!=reconstructed_batch_edge_targets.end()&&
                    !reconstructed->second.empty()) {
                ++downstream_treatment_reconstructed;
                downstream_batch_edge_targets.emplace(
                    requested,reconstructed->second);
            } else {
                ++downstream_treatment_missing;
                unresolved_downstream_edges.insert(requested);
                const auto& requested_reference=
                    downstream_edge_references.at(requested);
                ++missing_by_owner[requested_reference.owner_id];
                const auto separator=requested_reference.semantic_key.find(":ends:");
                const auto requested_between=requested_reference.semantic_key.substr(
                    0,separator);
                const auto requested_ends=separator==std::string::npos
                    ?std::string{}:requested_reference.semantic_key.substr(separator);
                bool between_matched=false,ends_matched=false;
                for(const auto& candidate:reconstructed_batch_edge_references) {
                    if(candidate.owner_id!=requested_reference.owner_id)continue;
                    const auto candidate_separator=
                        candidate.semantic_key.find(":ends:");
                    between_matched=between_matched||
                        candidate.semantic_key.substr(0,candidate_separator)==
                            requested_between;
                    ends_matched=ends_matched||
                        (candidate_separator!=std::string::npos&&
                         candidate.semantic_key.substr(candidate_separator)==
                            requested_ends);
                }
                downstream_between_matched+=between_matched;
                downstream_ends_matched+=ends_matched;
            }
        }
    }
    for(const auto& [owner,count]:missing_by_owner)
        std::cerr<<"batch unresolved downstream edges owner="<<owner
            <<" count="<<count<<'\n';
    const auto decode_set=[](std::string_view encoded) {
        std::set<std::string> result;
        std::size_t offset{};
        while(offset<encoded.size()) {
            const auto colon=encoded.find(':',offset);
            if(colon==std::string_view::npos)break;
            std::size_t length{};
            const auto length_text=encoded.substr(offset,colon-offset);
            const auto parsed=std::from_chars(length_text.data(),
                length_text.data()+length_text.size(),length);
            if(parsed.ec!=std::errc{}||colon+1+length>encoded.size())break;
            result.emplace(encoded.substr(colon+1,length));
            offset=colon+1+length;
        }
        return result;
    };
    const auto edge_sets=[&](const kernel::EdgeReference& reference) {
        std::pair<std::set<std::string>,std::set<std::string>> result;
        constexpr std::string_view prefix="boolean:add:intersection:between:";
        if(!reference.semantic_key.starts_with(prefix))return result;
        const auto separator=reference.semantic_key.find(":ends:",prefix.size());
        if(separator==std::string::npos)return result;
        result.first=decode_set(std::string_view(reference.semantic_key).substr(
            prefix.size(),separator-prefix.size()));
        result.second=decode_set(std::string_view(reference.semantic_key).substr(
            separator+6));
        return result;
    };
    std::map<std::string,TopoDS_Shape> expected_downstream_shapes;
    const auto expected_bound=bound_face_shape(local_body(last));
    for(const auto& [edge,reference]:expected_bound.edges) {
        const auto key=reference_key(reference);
        if(downstream_treatment_edges.contains(key))
            expected_downstream_shapes.emplace(key,edge);
    }
    struct EdgeMeasure {double length{};gp_Pnt center;};
    const auto measure_edge=[](const TopoDS_Shape& edge) {
        GProp_GProps properties;
        BRepGProp::LinearProperties(edge,properties);
        return EdgeMeasure{properties.Mass(),properties.CentreOfMass()};
    };
    std::vector<EdgeMeasure> batch_edge_measures(
        static_cast<std::size_t>(batch_edge_map.Extent())+1);
    for(int index=1;index<=batch_edge_map.Extent();++index)
        batch_edge_measures[static_cast<std::size_t>(index)]=
            measure_edge(batch_edge_map(index));
    std::size_t downstream_geometric_matches{};
    std::size_t geometric_between_matches{},geometric_end_matches{};
    std::size_t downstream_containment_unique{};
    std::map<int,std::size_t> contained_aliases_per_batch_edge;
    bool reported_geometric_difference=false;
    for(const auto& [key,reference]:downstream_edge_references) {
        if(!unresolved_downstream_edges.contains(key))continue;
        const auto expected_shape=expected_downstream_shapes.find(key);
        if(expected_shape==expected_downstream_shapes.end())continue;
        const auto expected_measure=measure_edge(expected_shape->second);
        std::vector<int> candidates;
        for(int index=1;index<=batch_edge_map.Extent();++index) {
            const auto& candidate=batch_edge_measures[static_cast<std::size_t>(index)];
            if(std::abs(candidate.length-expected_measure.length)<=
                    std::max(1.0e-7,expected_measure.length*1.0e-9)&&
               candidate.center.Distance(expected_measure.center)<=1.0e-7)
                candidates.push_back(index);
        }
        if(candidates.size()!=1)continue;
        const auto& candidate=
            reconstructed_batch_edge_by_index[static_cast<std::size_t>(
                candidates.front())];
        if(!candidate)continue;
        ++downstream_geometric_matches;
        const auto expected_sets=edge_sets(reference);
        const auto candidate_sets=edge_sets(*candidate);
        geometric_between_matches+=expected_sets.first==candidate_sets.first;
        geometric_end_matches+=expected_sets.second==candidate_sets.second;
        if(!reported_geometric_difference) {
            reported_geometric_difference=true;
            std::vector<std::string> missing_between,extra_between,
                missing_ends,extra_ends;
            std::ranges::set_difference(expected_sets.first,candidate_sets.first,
                std::back_inserter(missing_between));
            std::ranges::set_difference(candidate_sets.first,expected_sets.first,
                std::back_inserter(extra_between));
            std::ranges::set_difference(expected_sets.second,candidate_sets.second,
                std::back_inserter(missing_ends));
            std::ranges::set_difference(candidate_sets.second,expected_sets.second,
                std::back_inserter(extra_ends));
            std::cerr<<"batch geometric edge identity difference owner="
                <<reference.owner_id
                <<" expected_key_prefix="
                <<reference.semantic_key.substr(0,80)
                <<" expected_between="<<expected_sets.first.size()
                <<" candidate_between="<<candidate_sets.first.size()
                <<" missing_between="<<missing_between.size()
                <<" extra_between="<<extra_between.size()
                <<" expected_ends="<<expected_sets.second.size()
                <<" candidate_ends="<<candidate_sets.second.size()
                <<" missing_ends="<<missing_ends.size()
                <<" extra_ends="<<extra_ends.size()<<'\n';
        }
    }
    for(const auto& key:unresolved_downstream_edges) {
        const auto& reference=downstream_edge_references.at(key);
        const auto expected_shape=expected_downstream_shapes.find(key);
        if(expected_shape==expected_downstream_shapes.end())continue;
        const auto expected_measure=measure_edge(expected_shape->second);
        const BRepAdaptor_Curve expected_curve(
            TopoDS::Edge(expected_shape->second));
        const double first_parameter=expected_curve.FirstParameter();
        const double last_parameter=expected_curve.LastParameter();
        if(!std::isfinite(first_parameter)||!std::isfinite(last_parameter))continue;
        std::vector<int> candidates;
        const auto owner_prefix=reference.owner_id+'\x1f';
        for(int index=1;index<=batch_edge_map.Extent();++index) {
            const auto& ancestry=
                batch_edge_ancestry[static_cast<std::size_t>(index)];
            if(std::ranges::none_of(ancestry,[&](const auto& candidate) {
                    return candidate.starts_with(owner_prefix);
                }))continue;
            const auto& candidate_measure=
                batch_edge_measures[static_cast<std::size_t>(index)];
            if(candidate_measure.length+1.0e-7<expected_measure.length)continue;
            bool contains=true;
            for(int sample=0;sample<5&&contains;++sample) {
                const double parameter=first_parameter+
                    (last_parameter-first_parameter)*sample/4.;
                const auto probe=BRepBuilderAPI_MakeVertex(
                    expected_curve.Value(parameter)).Vertex();
                BRepExtrema_DistShapeShape distance(
                    probe,batch_edge_map(index));
                contains=distance.IsDone()&&distance.NbSolution()>0&&
                    distance.Value()<=1.0e-7;
            }
            if(contains)candidates.push_back(index);
        }
        if(candidates.size()==1) {
            ++downstream_containment_unique;
            ++contained_aliases_per_batch_edge[candidates.front()];
            downstream_batch_edge_targets.emplace(
                key,std::vector<int>{candidates.front()});
        }
    }
    std::map<std::size_t,std::size_t> containment_alias_histogram;
    for(const auto& [edge,count]:contained_aliases_per_batch_edge) {
        static_cast<void>(edge);
        ++containment_alias_histogram[count];
    }
    for(const auto& [aliases,edges]:containment_alias_histogram)
        std::cerr<<"batch containment alias group aliases="<<aliases
            <<" edges="<<edges<<'\n';
    // Classify the persisted treatment contract independently of how the edge
    // was located. Equal extent is a complete survivor. One or more proven
    // descendants with changed extent are the partial case; production must
    // retain only their surviving intervals. No proven descendant is the
    // absent case and must not rebind the feature to an unrelated edge.
    std::size_t downstream_treatment_full{},downstream_treatment_partial{};
    std::size_t downstream_treatment_absent{};
    for(const auto& key:downstream_treatment_edges) {
        const auto target=downstream_batch_edge_targets.find(key);
        const auto expected=expected_downstream_shapes.find(key);
        if(target==downstream_batch_edge_targets.end()||
           expected==expected_downstream_shapes.end()) {
            ++downstream_treatment_absent;
            continue;
        }
        if(target->second.size()!=1) {
            ++downstream_treatment_partial;
            continue;
        }
        const auto expected_measure=measure_edge(expected->second);
        const auto& actual=batch_edge_measures[
            static_cast<std::size_t>(target->second.front())];
        if(std::abs(actual.length-expected_measure.length)<=
                std::max(1.0e-7,expected_measure.length*1.0e-9)&&
           actual.center.Distance(expected_measure.center)<=1.0e-7)
            ++downstream_treatment_full;
        else ++downstream_treatment_partial;
    }
    if(downstream_treatment_full+downstream_treatment_partial+
            downstream_treatment_absent!=downstream_treatment_edges.size())
        throw std::runtime_error(
            "Batch treatment state did not classify every requested edge");
    double localized_treatment_ms=-1.;
    double localized_expected_minus=-1.;
    double localized_minus_expected=-1.;
    std::string localized_treatment_error;
    try {
        std::vector<std::size_t> treatment_indices;
        for(std::size_t index=last+1;index<operations.size();++index) {
            if(!std::holds_alternative<kernel::FilletRequest>(
                    operations[index].primitive))break;
            treatment_indices.push_back(index);
        }
        if(treatment_indices.empty())
            throw std::runtime_error("No downstream Fillet block");
        std::set<std::string> local_owners;
        for(const auto index:treatment_indices)
            for(const auto& edge:std::get<kernel::FilletRequest>(
                    operations[index].primitive).edges) {
                local_owners.insert(edge.owner_id);
                // This research path recovers every explicitly encoded
                // Boolean parent. The proposed edge-topology graph will carry
                // these parents structurally instead of parsing identity text.
                for(std::size_t source_index=first;source_index<=last;
                    ++source_index)
                    if(edge.semantic_key.find(
                           operations[source_index].owner_id)!=
                       std::string::npos)
                        local_owners.insert(
                            operations[source_index].owner_id);
            }
        TopTools_ListOfShape local_arguments,local_tools;
        bool first_local=true;
        for(const auto& owner:local_owners) {
            const auto source=feature_tools.find(owner);
            if(source==feature_tools.end())
                throw std::runtime_error(
                    "Localized Fillet source lies outside the Add batch");
            const auto shape=bound_face_shape(source->second).root;
            if(shape.IsNull())throw std::runtime_error(
                "Localized Fillet source has no native shape");
            if(first_local) {
                local_arguments.Append(shape);first_local=false;
            } else local_tools.Append(shape);
        }
        if(first_local||local_tools.IsEmpty())
            throw std::runtime_error(
                "Localized Fillet needs at least two source tools");
        const auto localized_start=Clock::now();
        BRepAlgoAPI_BooleanOperation local_fuse;
        local_fuse.SetOperation(BOPAlgo_FUSE);
        local_fuse.SetArguments(local_arguments);
        local_fuse.SetTools(local_tools);
        local_fuse.SetRunParallel(true);
        local_fuse.SetFuzzyValue(tolerance);
        local_fuse.Build();
        if(!local_fuse.IsDone()||local_fuse.Shape().IsNull())
            throw std::runtime_error("Localized source Fuse failed");
        ShapeUpgrade_UnifySameDomain local_source_unify(
            local_fuse.Shape(),true,true,false);
        local_source_unify.SetLinearTolerance(tolerance);
        local_source_unify.Build();
        TopoDS_Shape local_shape=local_source_unify.Shape();
        if(local_shape.IsNull()||!BRepCheck_Analyzer(local_shape).IsValid())
            throw std::runtime_error("Localized source Fuse is invalid");
        const auto containing_edge=[](const TopoDS_Shape& result,
                const TopoDS_Edge& expected) {
            GProp_GProps expected_properties;
            BRepGProp::LinearProperties(expected,expected_properties);
            BRepAdaptor_Curve expected_curve(expected);
            const double first=expected_curve.FirstParameter();
            const double last=expected_curve.LastParameter();
            if(!std::isfinite(first)||!std::isfinite(last))
                throw std::runtime_error(
                    "Localized Fillet reference has unbounded parameters");
            std::vector<TopoDS_Edge> matches;
            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(result,TopAbs_EDGE,edges);
            for(int index=1;index<=edges.Extent();++index) {
                const auto candidate=TopoDS::Edge(edges(index));
                GProp_GProps candidate_properties;
                BRepGProp::LinearProperties(candidate,candidate_properties);
                if(candidate_properties.Mass()+1.0e-7<
                   expected_properties.Mass())continue;
                bool contains=true;
                for(int sample=0;sample<5&&contains;++sample) {
                    const double parameter=first+(last-first)*sample/4.;
                    const auto point=BRepBuilderAPI_MakeVertex(
                        expected_curve.Value(parameter)).Vertex();
                    BRepExtrema_DistShapeShape distance(point,candidate);
                    contains=distance.IsDone()&&distance.NbSolution()>0&&
                        distance.Value()<=1.0e-7;
                }
                if(contains)matches.push_back(candidate);
            }
            if(matches.size()!=1)
                throw std::runtime_error(
                    "Localized Fillet edge has no unique geometric source");
            return matches.front();
        };
        for(const auto operation_index:treatment_indices) {
            const auto& request=std::get<kernel::FilletRequest>(
                operations[operation_index].primitive);
            if(request.mode!=kernel::FilletRequest::Mode::Constant)
                throw std::runtime_error(
                    "Localized prototype currently measures constant Fillets");
            const auto expected_input=bound_face_shape(
                local_body(operation_index-1));
            BRepFilletAPI_MakeFillet fillet(local_shape);
            TopTools_IndexedMapOfShape added;
            for(const auto& reference:request.edges) {
                const auto expected_edge=std::ranges::find_if(
                    expected_input.edges,[&](const auto& entry) {
                        return entry.second==reference;
                    });
                if(expected_edge==expected_input.edges.end())
                    throw std::runtime_error(
                        "Localized Fillet reference is absent from expected input");
                const auto edge=containing_edge(local_shape,
                    TopoDS::Edge(expected_edge->first));
                if(added.Contains(edge))continue;
                added.Add(edge);
                if(fillet.Contour(edge)==0)
                    fillet.Add(request.radius_start,edge);
            }
            fillet.SetParams(1.0e-2,
                std::max(1.0e-7,operations[operation_index].boolean_tolerance),
                1.0e-5,
                std::max(1.0e-7,operations[operation_index].boolean_tolerance),
                1.0e-5,1.0e-3);
            fillet.Build();
            if(!fillet.IsDone()||fillet.Shape().IsNull()) {
                std::cerr<<"localized Fillet status index="<<operation_index
                    <<" contours="<<fillet.NbContours()
                    <<" faulty-contours="<<fillet.NbFaultyContours()
                    <<" faulty-vertices="<<fillet.NbFaultyVertices()<<'\n';
                for(int faulty=1;faulty<=fillet.NbFaultyContours();++faulty) {
                    const int contour=fillet.FaultyContour(faulty);
                    std::cerr<<"localized Fillet faulty contour="<<contour
                        <<" status="<<static_cast<int>(
                            fillet.StripeStatus(contour))<<'\n';
                }
                throw std::runtime_error("Localized Fillet build failed");
            }
            ShapeUpgrade_UnifySameDomain fillet_unify(
                fillet.Shape(),true,true,false);
            fillet_unify.SetLinearTolerance(std::max(1.0e-7,
                operations[operation_index].boolean_tolerance));
            fillet_unify.Build();
            local_shape=fillet_unify.Shape();
            if(local_shape.IsNull()||!BRepCheck_Analyzer(local_shape).IsValid())
                throw std::runtime_error("Localized Fillet result is invalid");
        }
        TopTools_ListOfShape final_arguments,final_tools;
        final_arguments.Append(input_shape.root);
        for(std::size_t index=first;index<=last;++index) {
            if(local_owners.contains(operations[index].owner_id))continue;
            const auto source=feature_tools.find(operations[index].owner_id);
            if(source==feature_tools.end())continue;
            const auto shape=bound_face_shape(source->second).root;
            if(!shape.IsNull())final_tools.Append(shape);
        }
        final_tools.Append(local_shape);
        BRepAlgoAPI_BooleanOperation localized_fuse;
        localized_fuse.SetOperation(BOPAlgo_FUSE);
        localized_fuse.SetArguments(final_arguments);
        localized_fuse.SetTools(final_tools);
        localized_fuse.SetRunParallel(true);
        localized_fuse.SetFuzzyValue(tolerance);
        localized_fuse.Build();
        if(!localized_fuse.IsDone()||localized_fuse.Shape().IsNull())
            throw std::runtime_error("Localized final Fuse failed");
        ShapeUpgrade_UnifySameDomain localized_unify(
            localized_fuse.Shape(),true,true,false);
        localized_unify.SetLinearTolerance(tolerance);
        localized_unify.Build();
        const auto localized_shape=localized_unify.Shape();
        if(localized_shape.IsNull()||
           !BRepCheck_Analyzer(localized_shape).IsValid())
            throw std::runtime_error("Localized final result is invalid");
        localized_treatment_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-localized_start).count();
        const auto localized_expected=kernel::read_calculated_body_shape(
            local_body(treatment_indices.back()));
        localized_expected_minus=verify_equivalence
            ?difference_volume(localized_expected,localized_shape):-1.;
        localized_minus_expected=verify_equivalence
            ?difference_volume(localized_shape,localized_expected):-1.;
    } catch(const std::exception& error) {
        localized_treatment_error=error.what();
        std::cerr<<"localized Fillet probe failed: "
            <<localized_treatment_error<<'\n';
    }
    double downstream_batch_treatment_ms=-1.;
    double downstream_batch_expected_minus=-1.;
    double downstream_batch_minus_expected=-1.;
    std::size_t downstream_batch_result_faces{};
    std::string downstream_batch_error;
    double downstream_group_add_ms=-1.;
    double downstream_group_tool_ms=-1.;
    double downstream_group_fuse_ms=-1.;
    double downstream_group_unify_ms=-1.;
    double downstream_group_validate_ms=-1.;
    double downstream_group_parallel_validate_ms=-1.;
    double downstream_subtract_batch_ms=-1.;
    double downstream_subtract_expected_minus=-1.;
    double downstream_subtract_minus_expected=-1.;
    double downstream_final_add_ms=-1.;
    double downstream_final_tool_ms=-1.;
    double downstream_final_fuse_ms=-1.;
    double downstream_final_unify_ms=-1.;
    double downstream_final_validate_ms=-1.;
    double downstream_final_parallel_validate_ms=-1.;
    double downstream_final_expected_minus=-1.;
    double downstream_final_minus_expected=-1.;
    std::size_t downstream_replay_last=downstream_treatment_last;
    if(downstream_replay_last+1<operations.size()&&
       operations[downstream_replay_last+1].operation==
            kernel::BooleanOperation::Add&&
       std::holds_alternative<kernel::FeatureGroupRequest>(
            operations[downstream_replay_last+1].primitive))
        ++downstream_replay_last;
    const auto complete_local_boundaries=
        previous.back().body_boundaries.at(body_id).get();
    const auto& complete_cached_body=complete_local_boundaries.back();
    if(downstream_treatment_last>last&&
       downstream_batch_edge_targets.size()==downstream_treatment_edges.size()&&
       complete_cached_body.incremental_history&&
       complete_cached_body.incremental_history->frames.size()>last) {
        try {
            struct GroupFuseResult {
                TopoDS_Shape shape;
                double total_ms{};
                double tool_ms{};
                double fuse_ms{};
                double unify_ms{};
                double validate_ms{};
                double parallel_validate_ms{};
            };
            const auto fuse_group_children_once=[&](const TopoDS_Shape& base,
                    std::size_t operation_index) {
                const auto* group=std::get_if<kernel::FeatureGroupRequest>(
                    &operations.at(operation_index).primitive);
                if(!group||group->children.empty())
                    throw std::runtime_error(
                        "Single Fuse probe requires a nonempty Feature group");
                const auto& references=
                    complete_local_boundaries.at(operation_index-1);
                const auto start=Clock::now();
                std::vector<std::future<kernel::BodyResult>> pending;
                pending.reserve(group->children.size());
                for(const auto& child:group->children) {
                    auto child_operation=operations.at(operation_index);
                    auto child_group=*group;
                    child_group.children={child};
                    child_operation.primitive=std::move(child_group);
                    pending.emplace_back(std::async(std::launch::async,
                        [child_operation=std::move(child_operation),
                         &references] {
                            kernel::OcctKernel child_kernel;
                            return child_kernel.calculate_feature_tool(
                                child_operation,references,{},references,{});
                        }));
                }
                TopTools_ListOfShape arguments,group_tools;
                arguments.Append(base);
                for(auto& future:pending) {
                    auto child_result=future.get();
                    const auto child_shape=bound_face_shape(child_result).root;
                    if(child_shape.IsNull())
                        throw std::runtime_error(
                            "Feature group child produced no native shape");
                    group_tools.Append(child_shape);
                }
                const auto tools_ready=Clock::now();
                BRepAlgoAPI_BooleanOperation fuse_group;
                fuse_group.SetOperation(BOPAlgo_FUSE);
                fuse_group.SetArguments(arguments);
                fuse_group.SetTools(group_tools);
                fuse_group.SetRunParallel(true);
                fuse_group.SetToFillHistory(true);
                const auto group_tolerance=std::max(1.0e-7,
                    operations.at(operation_index).boolean_tolerance);
                fuse_group.SetFuzzyValue(group_tolerance);
                fuse_group.Build();
                if(!fuse_group.IsDone()||fuse_group.Shape().IsNull())
                    throw std::runtime_error(
                        "Single Feature-group Fuse failed");
                const auto fuse_ready=Clock::now();
                ShapeUpgrade_UnifySameDomain group_unify(
                    fuse_group.Shape(),true,true,false);
                group_unify.SetLinearTolerance(group_tolerance);
                group_unify.Build();
                auto result=group_unify.Shape();
                const auto unify_ready=Clock::now();
                if(result.IsNull()||!BRepCheck_Analyzer(result).IsValid())
                    throw std::runtime_error(
                        "Single Feature-group Fuse produced an invalid shape");
                const auto validated=Clock::now();
                if(!BRepCheck_Analyzer(result,true,true,false).IsValid())
                    throw std::runtime_error(
                        "Parallel validation disagreed for Feature-group Fuse");
                const auto parallel_validated=Clock::now();
                return GroupFuseResult{
                    std::move(result),
                    std::chrono::duration<double,std::milli>(validated-start).count(),
                    std::chrono::duration<double,std::milli>(tools_ready-start).count(),
                    std::chrono::duration<double,std::milli>(fuse_ready-tools_ready).count(),
                    std::chrono::duration<double,std::milli>(unify_ready-fuse_ready).count(),
                    std::chrono::duration<double,std::milli>(validated-unify_ready).count(),
                    std::chrono::duration<double,std::milli>(parallel_validated-validated).count()};
            };
            kernel::BodyResult batch_boundary;
            batch_boundary.kernel_shape=batch_stream.str();
            batch_boundary.solid_present=true;
            batch_boundary.source_fingerprint=
                complete_local_boundaries.at(last).source_fingerprint;
            std::vector<kernel::BodyResult::KernelBinding<kernel::FaceReference>>
                face_bindings;
            face_bindings.reserve(static_cast<std::size_t>(batch_face_map.Extent()));
            for(int index=1;index<=batch_face_map.Extent();++index) {
                auto reference=canonical_batch_faces[static_cast<std::size_t>(index)]
                    .value_or(kernel::FaceReference{"batch-research",
                        "face:"+std::to_string(index),{}});
                std::ostringstream locator;
                batch_archive.Write(batch_face_map(index),locator);
                face_bindings.push_back({locator.str(),std::move(reference)});
            }
            batch_boundary.kernel_faces=std::move(face_bindings);
            std::vector<kernel::BodyResult::KernelBinding<kernel::EdgeReference>>
                edge_bindings;
            for(int index=1;index<=batch_edge_map.Extent();++index) {
                std::vector<kernel::EdgeReference> references;
                for(const auto& [key,targets]:downstream_batch_edge_targets)
                    if(std::ranges::find(targets,index)!=targets.end())
                        references.push_back(
                        downstream_edge_references.at(key));
                if(references.empty()) {
                    const auto& sources=
                        batch_edge_sources[static_cast<std::size_t>(index)];
                    if(!sources.empty())references.push_back(sources.front());
                    else references.push_back({"batch-research",
                        "edge:"+std::to_string(index),{}});
                }
                std::ostringstream locator;
                batch_archive.Write(batch_edge_map(index),locator);
                for(auto& reference:references)
                    edge_bindings.push_back({locator.str(),std::move(reference)});
            }
            batch_boundary.kernel_edges=std::move(edge_bindings);
            TopTools_IndexedMapOfShape batch_vertex_map;
            TopExp::MapShapes(unified,TopAbs_VERTEX,batch_vertex_map);
            std::vector<kernel::BodyResult::KernelBinding<kernel::VertexReference>>
                vertex_bindings;
            vertex_bindings.reserve(static_cast<std::size_t>(
                batch_vertex_map.Extent()));
            for(int index=1;index<=batch_vertex_map.Extent();++index) {
                std::ostringstream locator;
                batch_archive.Write(batch_vertex_map(index),locator);
                vertex_bindings.push_back({locator.str(),{"batch-research",
                    "vertex:"+std::to_string(index),{}}});
            }
            batch_boundary.kernel_vertices=std::move(vertex_bindings);

            auto history=*complete_cached_body.incremental_history;
            auto frames=history.frames.get();
            auto native_boundary=batch_boundary;
            native_boundary.incremental_history.reset();
            frames[last].native_body=kernel::BodySnapshot(
                std::move(native_boundary));
            frames[last].hidden_edges.clear();
            history.frames=std::move(frames);
            batch_boundary.incremental_history=
                std::make_shared<const kernel::KernelHistoryCache>(
                    std::move(history));
            auto previous_document=previous.back();
            auto local_boundaries=
                previous_document.body_boundaries.at(body_id).get();
            if(local_boundaries.size()<=last)
                throw std::runtime_error(
                    "Complete Body cache does not contain the batch boundary");
            local_boundaries.resize(last+1);
            local_boundaries.back()=std::move(batch_boundary);
            previous_document.body_boundaries.insert_or_assign(
                body_id,std::move(local_boundaries));
            std::vector<kernel::BodyResult> batch_prefix{
                std::move(previous_document)};
            const std::vector<kernel::HistoryOperation> treatment_operations(
                operations.begin(),operations.begin()+
                    static_cast<std::ptrdiff_t>(downstream_treatment_last+1));
            kernel::OcctKernel treatment_kernel;
            const auto treatment_start=Clock::now();
            const auto treated=treatment_kernel.evaluate_history_incremental(
                treatment_operations,batch_prefix);
            downstream_batch_treatment_ms=
                std::chrono::duration<double,std::milli>(
                    Clock::now()-treatment_start).count();
            if(treated.size()!=downstream_treatment_last+1||
               !treated.back().calculation_errors.empty())
                throw std::runtime_error(
                    "Batched downstream treatment did not calculate");
            auto batch_treated_shape=kernel::read_calculated_body_shape(
                treated.back());
            if(downstream_replay_last>downstream_treatment_last) {
                auto group=fuse_group_children_once(
                    batch_treated_shape,downstream_replay_last);
                batch_treated_shape=std::move(group.shape);
                downstream_group_add_ms=group.total_ms;
                downstream_group_tool_ms=group.tool_ms;
                downstream_group_fuse_ms=group.fuse_ms;
                downstream_group_unify_ms=group.unify_ms;
                downstream_group_validate_ms=group.validate_ms;
                downstream_group_parallel_validate_ms=group.parallel_validate_ms;
            }
            const auto treated_shape=kernel::read_calculated_body_shape(
                complete_local_boundaries.at(downstream_replay_last));
            TopTools_IndexedMapOfShape treated_faces;
            TopExp::MapShapes(batch_treated_shape,TopAbs_FACE,treated_faces);
            downstream_batch_result_faces=static_cast<std::size_t>(
                treated_faces.Extent());
            downstream_batch_expected_minus=verify_equivalence
                ?difference_volume(treated_shape,batch_treated_shape):-1.;
            downstream_batch_minus_expected=verify_equivalence
                ?difference_volume(batch_treated_shape,treated_shape):-1.;
            if(downstream_replay_last+5<operations.size()) {
                const std::size_t subtract_first=downstream_replay_last+1;
                const std::size_t subtract_last=subtract_first+3;
                kernel::OcctKernel suffix_kernel;
                std::map<std::string,kernel::BodyResult> suffix_tools;
                std::vector<TopoDS_Shape> cut_shapes;
                const auto suffix_start=Clock::now();
                for(std::size_t index=subtract_first;index<=subtract_last;
                    ++index) {
                    kernel::BodyResult tool;
                    if(operations[index].feature_copy) {
                        const auto& copy=*operations[index].feature_copy;
                        const auto source=suffix_tools.find(
                            copy.source_feature_id);
                        if(source==suffix_tools.end())
                            throw std::runtime_error(
                                "Subtract copy source is unavailable");
                        tool=copy.combination==kernel::BodyCombination::Pattern
                            ?suffix_kernel.pattern_body(source->second,
                                copy.pattern,operations[index].owner_id)
                            :suffix_kernel.mirror_body(source->second,
                                copy.mirror_plane,operations[index].owner_id);
                    } else {
                        tool=suffix_kernel.calculate_feature_tool(
                            operations[index],
                            complete_local_boundaries.at(index-1),{},
                            complete_local_boundaries.at(index-1),{});
                    }
                    suffix_tools.emplace(operations[index].owner_id,tool);
                    const auto bound=bound_face_shape(tool);
                    if(bound.root.IsNull())
                        throw std::runtime_error(
                            "Subtract batch tool has no native shape");
                    cut_shapes.push_back(bound.root);
                }
                TopTools_ListOfShape cut_arguments,cut_tools;
                cut_arguments.Append(batch_treated_shape);
                for(const auto& shape:cut_shapes)cut_tools.Append(shape);
                BRepAlgoAPI_BooleanOperation cut_batch;
                cut_batch.SetOperation(BOPAlgo_CUT);
                cut_batch.SetArguments(cut_arguments);
                cut_batch.SetTools(cut_tools);
                cut_batch.SetRunParallel(true);
                cut_batch.SetToFillHistory(true);
                double cut_tolerance=1.0e-7;
                for(std::size_t index=subtract_first;index<=subtract_last;
                    ++index)
                    cut_tolerance=std::max(cut_tolerance,
                        operations[index].boolean_tolerance);
                cut_batch.SetFuzzyValue(cut_tolerance);
                cut_batch.Build();
                if(!cut_batch.IsDone()||cut_batch.Shape().IsNull())
                    throw std::runtime_error("Subtract batch failed");
                ShapeUpgrade_UnifySameDomain cut_unify(
                    cut_batch.Shape(),true,true,false);
                cut_unify.SetLinearTolerance(cut_tolerance);
                cut_unify.Build();
                const auto cut_shape=cut_unify.Shape();
                if(cut_shape.IsNull()||
                   !BRepCheck_Analyzer(cut_shape).IsValid())
                    throw std::runtime_error(
                        "Subtract batch produced an invalid shape");
                downstream_subtract_batch_ms=
                    std::chrono::duration<double,std::milli>(
                        Clock::now()-suffix_start).count();
                const auto expected_cut=kernel::read_calculated_body_shape(
                    complete_local_boundaries.at(subtract_last));
                downstream_subtract_expected_minus=verify_equivalence
                    ?difference_volume(expected_cut,cut_shape):-1.;
                downstream_subtract_minus_expected=verify_equivalence
                    ?difference_volume(cut_shape,expected_cut):-1.;

                const std::size_t final_index=subtract_last+1;
                auto final=fuse_group_children_once(
                    cut_shape,final_index);
                downstream_final_add_ms=final.total_ms;
                downstream_final_tool_ms=final.tool_ms;
                downstream_final_fuse_ms=final.fuse_ms;
                downstream_final_unify_ms=final.unify_ms;
                downstream_final_validate_ms=final.validate_ms;
                downstream_final_parallel_validate_ms=final.parallel_validate_ms;
                const auto expected_final=kernel::read_calculated_body_shape(
                    complete_local_boundaries.at(final_index));
                downstream_final_expected_minus=verify_equivalence
                    ?difference_volume(expected_final,final.shape):-1.;
                downstream_final_minus_expected=verify_equivalence
                    ?difference_volume(final.shape,expected_final):-1.;
            }
        } catch(const std::exception& error) {
            downstream_batch_error=error.what();
            std::cerr<<"batch downstream treatment failed: "
                <<downstream_batch_error<<'\n';
        }
    }
    const auto identity_bound=bound_face_shape(identity_body(last));
    std::set<std::string> expected_face_references;
    for(const auto& binding:identity_body(last).kernel_faces)
        expected_face_references.insert(reference_key(binding.reference));
    std::vector<std::string> retained_face_references;
    std::ranges::set_intersection(expected_face_references,
        propagated_face_references,
        std::back_inserter(retained_face_references));
    std::vector<std::string> missing_face_references;
    std::ranges::set_difference(expected_face_references,
        propagated_face_references,
        std::back_inserter(missing_face_references));
    std::map<std::string,std::size_t> missing_faces_by_owner;
    std::size_t reported_missing_faces{};
    for(const auto& binding:identity_body(last).kernel_faces) {
        const auto key=reference_key(binding.reference);
        if(!std::ranges::binary_search(missing_face_references,key))continue;
        ++missing_faces_by_owner[binding.reference.owner_id];
        if(reported_missing_faces++<20)
            std::cerr<<"batch missing face identity owner="
                <<binding.reference.owner_id<<" key="
                <<binding.reference.semantic_key.substr(0,1000)<<'\n';
    }
    for(const auto& [owner,count]:missing_faces_by_owner)
        std::cerr<<"batch missing face owner="<<owner
            <<" count="<<count<<'\n';
    std::map<std::string,TopoDS_Shape> expected_face_shapes;
    for(const auto& [face,reference]:identity_bound.faces)
        expected_face_shapes.emplace(reference_key(reference),face);
    const auto face_measure=[](const TopoDS_Shape& face) {
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face,properties);
        return std::abs(properties.Mass());
    };
    const auto face_samples=[](const TopoDS_Face& face) {
        std::vector<gp_Pnt> result;
        TopLoc_Location location;
        const auto triangulation=BRep_Tool::Triangulation(face,location);
        if(!triangulation.IsNull()&&triangulation->NbNodes()>0) {
            const std::array<int,5> nodes{1,
                std::max(1,triangulation->NbNodes()/4),
                std::max(1,triangulation->NbNodes()/2),
                std::max(1,3*triangulation->NbNodes()/4),
                triangulation->NbNodes()};
            for(const int node:nodes) {
                const auto point=triangulation->Node(node).Transformed(
                    location.Transformation());
                if(std::ranges::none_of(result,[&](const auto& current) {
                        return current.Distance(point)<=1.0e-12;
                    }))result.push_back(point);
            }
            if(triangulation->NbTriangles()>0) {
                int first{},second{},third{};
                triangulation->Triangle(1).Get(first,second,third);
                gp_XYZ center=triangulation->Node(first).XYZ()+
                    triangulation->Node(second).XYZ()+
                    triangulation->Node(third).XYZ();
                center/=3.;
                result.emplace_back(center);
                result.back().Transform(location.Transformation());
            }
        }
        if(result.empty())for(TopExp_Explorer vertex(face,TopAbs_VERTEX);
                vertex.More();vertex.Next())
            result.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())));
        return result;
    };
    std::vector<double> raw_batch_face_areas(
        static_cast<std::size_t>(raw_batch_face_map.Extent())+1);
    for(int index=1;index<=raw_batch_face_map.Extent();++index)
        raw_batch_face_areas[static_cast<std::size_t>(index)]=
            face_measure(raw_batch_face_map(index));
    std::size_t missing_face_raw_unique_containment{};
    std::size_t missing_face_raw_ambiguous_containment{};
    std::size_t missing_face_raw_absent_containment{};
    std::size_t missing_face_fragment_sets{};
    std::size_t missing_face_fragment_area_matches{};
    std::size_t missing_face_root_unique{};
    std::size_t missing_face_root_ambiguous{};
    std::size_t missing_face_root_absent{};
    std::size_t missing_face_root_source_exists{};
    std::size_t missing_face_boundary_unique{};
    std::size_t missing_face_boundary_tied{};
    std::size_t missing_face_boundary_empty{};
    std::set<std::string> source_face_reference_keys;
    for(const auto& [face,reference]:source_faces) {
        static_cast<void>(face);
        source_face_reference_keys.insert(reference_key(reference));
    }
    std::set<std::string> raw_face_reference_keys;
    std::map<std::string,std::set<int>> expected_face_targets;
    for(std::size_t index=1;index<raw_batch_face_ancestry.size();++index)
        for(const auto& reference:raw_batch_face_ancestry[index]) {
            raw_face_reference_keys.insert(reference);
            if(expected_face_references.contains(reference))
                expected_face_targets[reference].insert(
                    static_cast<int>(index));
        }
    const auto decode_face_reference=[](std::string_view encoded)
            ->std::optional<kernel::FaceReference> {
        std::size_t offset{};
        const auto field=[&]() -> std::optional<std::string> {
            const auto separator=encoded.find(':',offset);
            if(separator==std::string_view::npos)return std::nullopt;
            std::size_t length{};
            const auto text=encoded.substr(offset,separator-offset);
            const auto parsed=std::from_chars(
                text.data(),text.data()+text.size(),length);
            if(parsed.ec!=std::errc{}||
               parsed.ptr!=text.data()+text.size()||
               separator+1+length>encoded.size())return std::nullopt;
            offset=separator+1+length;
            return std::string(encoded.substr(separator+1,length));
        };
        const auto owner=field(),semantic=field(),instance=field();
        if(!owner||!semantic||!instance)return std::nullopt;
        return kernel::FaceReference{*owner,*semantic,*instance};
    };
    const auto root_face_reference=[&](kernel::FaceReference reference) {
        constexpr std::string_view marker=":split-face:from:";
        for(std::size_t depth=0;depth<operations.size();++depth) {
            const auto offset=reference.semantic_key.find(marker);
            if(offset==std::string::npos)break;
            const auto parents=std::string_view(reference.semantic_key).substr(
                offset+marker.size());
            const auto separator=parents.find(':');
            if(separator==std::string_view::npos)break;
            std::size_t length{};
            const auto length_text=parents.substr(0,separator);
            const auto parsed=std::from_chars(length_text.data(),
                length_text.data()+length_text.size(),length);
            if(parsed.ec!=std::errc{}||
               separator+1+length>parents.size())break;
            const auto parent=decode_face_reference(
                parents.substr(separator+1,length));
            if(!parent)break;
            reference=*parent;
        }
        return reference;
    };
    const auto decode_boundary_edge_reference=[](std::string_view encoded)
            ->std::optional<kernel::EdgeReference> {
        std::size_t offset{};
        const auto field=[&]() -> std::optional<std::string> {
            const auto separator=encoded.find(':',offset);
            if(separator==std::string_view::npos)return std::nullopt;
            std::size_t length{};
            const auto text=encoded.substr(offset,separator-offset);
            const auto parsed=std::from_chars(
                text.data(),text.data()+text.size(),length);
            if(parsed.ec!=std::errc{}||
               parsed.ptr!=text.data()+text.size()||
               separator+1+length>encoded.size())return std::nullopt;
            offset=separator+1+length;
            return std::string(encoded.substr(separator+1,length));
        };
        const auto owner=field(),semantic=field(),instance=field();
        if(!owner||!semantic||!instance)return std::nullopt;
        return kernel::EdgeReference{*owner,*semantic,*instance};
    };
    const auto root_boundary_edge_reference=[&](kernel::EdgeReference reference) {
        constexpr std::string_view marker=":split-edge:from:";
        for(std::size_t depth=0;depth<operations.size();++depth) {
            const auto offset=reference.semantic_key.find(marker);
            if(offset==std::string::npos)break;
            const auto parent=decode_boundary_edge_reference(
                std::string_view(reference.semantic_key).substr(
                    offset+marker.size()));
            if(!parent)break;
            reference=*parent;
        }
        return reference;
    };
    for(const auto& binding:identity_body(last).kernel_faces) {
        const auto key=reference_key(binding.reference);
        if(!std::ranges::binary_search(missing_face_references,key))continue;
        const auto expected=expected_face_shapes.find(key);
        if(expected==expected_face_shapes.end()) {
            ++missing_face_raw_absent_containment;continue;
        }
        const double expected_area=face_measure(expected->second);
        const auto samples=face_samples(TopoDS::Face(expected->second));
        if(samples.empty()) {
            ++missing_face_raw_absent_containment;continue;
        }
        const auto root=reference_key(root_face_reference(binding.reference));
        missing_face_root_source_exists+=
            source_face_reference_keys.contains(root);
        std::vector<int> root_candidates;
        for(int index=1;index<=raw_batch_face_map.Extent();++index)
            if(raw_batch_face_ancestry[static_cast<std::size_t>(index)]
                    .contains(root))root_candidates.push_back(index);
        if(root_candidates.size()==1)++missing_face_root_unique;
        else if(root_candidates.empty())++missing_face_root_absent;
        else ++missing_face_root_ambiguous;
        std::set<std::string> old_boundary_roots;
        TopTools_IndexedMapOfShape old_face_edges;
        TopExp::MapShapes(expected->second,TopAbs_EDGE,old_face_edges);
        for(const auto& [edge,reference]:identity_bound.edges)
            if(old_face_edges.Contains(edge))old_boundary_roots.insert(
                reference_key(root_boundary_edge_reference(reference)));
        std::size_t best_boundary_score{};
        std::size_t best_boundary_candidates{};
        std::vector<int> best_boundary_indices;
        for(const int candidate:root_candidates) {
            std::set<std::string> candidate_boundary_roots;
            for(TopExp_Explorer edge(raw_batch_face_map(candidate),TopAbs_EDGE);
                    edge.More();edge.Next()) {
                const int edge_index=raw_batch_edge_map.FindIndex(edge.Current());
                if(edge_index)candidate_boundary_roots.insert(
                    raw_batch_edge_ancestry[
                        static_cast<std::size_t>(edge_index)].begin(),
                    raw_batch_edge_ancestry[
                        static_cast<std::size_t>(edge_index)].end());
            }
            std::size_t score{};
            for(const auto& boundary:old_boundary_roots)
                score+=candidate_boundary_roots.contains(boundary);
            if(score>best_boundary_score) {
                best_boundary_score=score;best_boundary_candidates=1;
                best_boundary_indices={candidate};
            } else if(score==best_boundary_score&&score>0)
                {++best_boundary_candidates;
                 best_boundary_indices.push_back(candidate);}
        }
        if(best_boundary_score==0)++missing_face_boundary_empty;
        else if(best_boundary_candidates==1)++missing_face_boundary_unique;
        else ++missing_face_boundary_tied;
        std::vector<int> candidates;
        for(int index=1;index<=raw_batch_face_map.Extent();++index) {
            if(raw_batch_face_areas[static_cast<std::size_t>(index)]+1.0e-7<
                    expected_area)continue;
            bool contains=true;
            for(const auto& point:samples) {
                const auto probe=BRepBuilderAPI_MakeVertex(point).Vertex();
                BRepExtrema_DistShapeShape distance(
                    probe,raw_batch_face_map(index));
                contains=distance.IsDone()&&distance.NbSolution()>0&&
                    distance.Value()<=1.0e-7;
                if(!contains)break;
            }
            if(contains)candidates.push_back(index);
        }
        if(candidates.size()==1) {
            ++missing_face_raw_unique_containment;
            expected_face_targets[key].insert(candidates.front());
        }
        else if(candidates.empty()) {
            ++missing_face_raw_absent_containment;
            double fragment_area{};
            std::vector<int> fragments;
            for(int index=1;index<=raw_batch_face_map.Extent();++index) {
                if(raw_batch_face_areas[static_cast<std::size_t>(index)]>
                        expected_area+1.0e-7)continue;
                const auto fragment_samples=face_samples(
                    TopoDS::Face(raw_batch_face_map(index)));
                if(fragment_samples.empty())continue;
                bool contained=true;
                for(const auto& point:fragment_samples) {
                    const auto probe=BRepBuilderAPI_MakeVertex(point).Vertex();
                    BRepExtrema_DistShapeShape distance(
                        probe,expected->second);
                    contained=distance.IsDone()&&distance.NbSolution()>0&&
                        distance.Value()<=1.0e-7;
                    if(!contained)break;
                }
                if(contained) {
                    fragments.push_back(index);
                    fragment_area+=raw_batch_face_areas[
                        static_cast<std::size_t>(index)];
                }
            }
            if(!fragments.empty())++missing_face_fragment_sets;
            const double area_difference=std::abs(fragment_area-expected_area);
            if(!fragments.empty()&&area_difference<=
                    std::max(1.0e-7,expected_area*1.0e-9)) {
                ++missing_face_fragment_area_matches;
                expected_face_targets[key].insert(
                    fragments.begin(),fragments.end());
            } else if(best_boundary_indices.size()==1)
                expected_face_targets[key].insert(
                    best_boundary_indices.front());
            std::cerr<<"batch missing face fragments owner="
                <<binding.reference.owner_id<<" fragments="
                <<fragments.size()<<" expected_area="<<expected_area
                <<" fragment_area="<<fragment_area
                <<" difference="<<area_difference
                <<" root_candidates="<<root_candidates.size()
                <<" boundary_roots="<<old_boundary_roots.size()
                <<" best_boundary_score="<<best_boundary_score
                <<" best_boundary_candidates="<<best_boundary_candidates
                <<'\n';
        } else ++missing_face_raw_ambiguous_containment;
    }
    std::set<std::string> expected_edge_references;
    for(const auto& binding:identity_body(last).kernel_edges)
        expected_edge_references.insert(reference_key(binding.reference));
    std::set<std::string> mapped_edge_references;
    for(std::size_t index=1;index<batch_edge_ancestry.size();++index)
        mapped_edge_references.insert(batch_edge_ancestry[index].begin(),
            batch_edge_ancestry[index].end());
    for(const auto& [reference,targets]:reconstructed_batch_edge_targets)
        if(!targets.empty())mapped_edge_references.insert(reference);
    std::vector<std::string> retained_edge_references,
        missing_edge_references;
    std::ranges::set_intersection(expected_edge_references,
        mapped_edge_references,
        std::back_inserter(retained_edge_references));
    std::ranges::set_difference(expected_edge_references,
        mapped_edge_references,
        std::back_inserter(missing_edge_references));
    std::map<std::string,std::size_t> missing_edges_by_owner;
    std::size_t reported_missing_edges{};
    for(const auto& binding:identity_body(last).kernel_edges) {
        const auto key=reference_key(binding.reference);
        if(!std::ranges::binary_search(missing_edge_references,key))continue;
        ++missing_edges_by_owner[binding.reference.owner_id];
        if(reported_missing_edges++<20)
            std::cerr<<"batch missing edge identity owner="
                <<binding.reference.owner_id<<" key="
                <<binding.reference.semantic_key.substr(0,1000)<<'\n';
    }
    for(const auto& [owner,count]:missing_edges_by_owner)
        std::cerr<<"batch missing edge owner="<<owner
            <<" count="<<count<<'\n';
    const auto decode_edge_reference=[](std::string_view encoded)
            ->std::optional<kernel::EdgeReference> {
        std::size_t offset{};
        const auto field=[&]() -> std::optional<std::string> {
            const auto separator=encoded.find(':',offset);
            if(separator==std::string_view::npos)return std::nullopt;
            std::size_t length{};
            const auto text=encoded.substr(offset,separator-offset);
            const auto parsed=std::from_chars(
                text.data(),text.data()+text.size(),length);
            if(parsed.ec!=std::errc{}||
               parsed.ptr!=text.data()+text.size()||
               separator+1+length>encoded.size())return std::nullopt;
            offset=separator+1+length;
            return std::string(encoded.substr(separator+1,length));
        };
        const auto owner=field(),semantic=field(),instance=field();
        if(!owner||!semantic||!instance)return std::nullopt;
        return kernel::EdgeReference{*owner,*semantic,*instance};
    };
    const auto root_edge_reference=[&](kernel::EdgeReference reference) {
        constexpr std::string_view marker=":split-edge:from:";
        for(std::size_t depth=0;depth<operations.size();++depth) {
            const auto offset=reference.semantic_key.find(marker);
            if(offset==std::string::npos)break;
            const auto parent=decode_edge_reference(
                std::string_view(reference.semantic_key).substr(
                    offset+marker.size()));
            if(!parent)break;
            reference=*parent;
        }
        return reference;
    };
    std::map<std::string,TopoDS_Shape> expected_edge_shapes;
    for(const auto& [edge,reference]:identity_bound.edges)
        expected_edge_shapes.emplace(reference_key(reference),edge);
    std::map<std::string,std::set<int>> expected_edge_targets;
    for(std::size_t index=1;index<raw_batch_edge_ancestry.size();++index)
        for(const auto& reference:raw_batch_edge_ancestry[index])
            if(expected_edge_references.contains(reference))
                expected_edge_targets[reference].insert(
                    static_cast<int>(index));
    std::size_t missing_edge_unique_containment{};
    std::size_t missing_edge_ambiguous_containment{};
    std::size_t missing_edge_absent_containment{};
    std::map<int,std::size_t> missing_aliases_per_batch_edge;
    std::size_t missing_edge_raw_unique_containment{};
    std::size_t missing_edge_raw_ambiguous_containment{};
    std::size_t missing_edge_raw_absent_containment{};
    std::size_t missing_edge_global_unique_containment{};
    std::size_t missing_edge_global_ambiguous_containment{};
    std::size_t missing_edge_global_absent_containment{};
    std::vector<EdgeMeasure> raw_batch_edge_measures(
        static_cast<std::size_t>(raw_batch_edge_map.Extent())+1);
    for(int index=1;index<=raw_batch_edge_map.Extent();++index)
        raw_batch_edge_measures[static_cast<std::size_t>(index)]=
            measure_edge(raw_batch_edge_map(index));
    std::size_t missing_edge_reconstructed_roots{};
    std::map<std::string,std::size_t> missing_edges_by_root;
    std::map<std::string,std::size_t> unmapped_edges_by_root;
    std::map<std::string,kernel::EdgeReference> missing_root_references;
    for(const auto& binding:identity_body(last).kernel_edges) {
        const auto key=reference_key(binding.reference);
        if(!std::ranges::binary_search(missing_edge_references,key))continue;
        const auto expected=expected_edge_shapes.find(key);
        if(expected==expected_edge_shapes.end()) {
            ++missing_edge_absent_containment;continue;
        }
        const auto root_reference=root_edge_reference(binding.reference);
        const auto root=reference_key(root_reference);
        missing_root_references.emplace(root,root_reference);
        ++missing_edges_by_root[root];
        missing_edge_reconstructed_roots+=
            reconstructed_batch_edge_targets.contains(root);
        const auto expected_measure=measure_edge(expected->second);
        const BRepAdaptor_Curve expected_curve(
            TopoDS::Edge(expected->second));
        const double first_parameter=expected_curve.FirstParameter();
        const double last_parameter=expected_curve.LastParameter();
        if(!std::isfinite(first_parameter)||!std::isfinite(last_parameter)) {
            ++missing_edge_absent_containment;continue;
        }
        std::vector<int> candidates;
        for(int index=1;index<=batch_edge_map.Extent();++index) {
            if(!batch_edge_ancestry[static_cast<std::size_t>(index)]
                    .contains(root))continue;
            const auto& candidate_measure=
                batch_edge_measures[static_cast<std::size_t>(index)];
            if(candidate_measure.length+1.0e-7<expected_measure.length)continue;
            bool contains=true;
            for(int sample=0;sample<5&&contains;++sample) {
                const double parameter=first_parameter+
                    (last_parameter-first_parameter)*sample/4.;
                const auto probe=BRepBuilderAPI_MakeVertex(
                    expected_curve.Value(parameter)).Vertex();
                BRepExtrema_DistShapeShape distance(
                    probe,batch_edge_map(index));
                contains=distance.IsDone()&&distance.NbSolution()>0&&
                    distance.Value()<=1.0e-7;
            }
            if(contains)candidates.push_back(index);
        }
        if(candidates.size()==1) {
            ++missing_edge_unique_containment;
            ++missing_aliases_per_batch_edge[candidates.front()];
        } else if(candidates.empty())++missing_edge_absent_containment;
        else ++missing_edge_ambiguous_containment;
        candidates.clear();
        for(int index=1;index<=raw_batch_edge_map.Extent();++index) {
            if(!raw_batch_edge_ancestry[static_cast<std::size_t>(index)]
                    .contains(root))continue;
            const auto& candidate_measure=
                raw_batch_edge_measures[static_cast<std::size_t>(index)];
            if(candidate_measure.length+1.0e-7<expected_measure.length)continue;
            bool contains=true;
            for(int sample=0;sample<5&&contains;++sample) {
                const double parameter=first_parameter+
                    (last_parameter-first_parameter)*sample/4.;
                const auto probe=BRepBuilderAPI_MakeVertex(
                    expected_curve.Value(parameter)).Vertex();
                BRepExtrema_DistShapeShape distance(
                    probe,raw_batch_edge_map(index));
                contains=distance.IsDone()&&distance.NbSolution()>0&&
                    distance.Value()<=1.0e-7;
            }
            if(contains)candidates.push_back(index);
        }
        if(candidates.size()==1) {
            ++missing_edge_raw_unique_containment;
            expected_edge_targets[key].insert(candidates.front());
        }
        else if(candidates.empty()) {
            ++missing_edge_raw_absent_containment;
            for(int index=1;index<=raw_batch_edge_map.Extent();++index) {
                const auto& candidate_measure=
                    raw_batch_edge_measures[static_cast<std::size_t>(index)];
                if(candidate_measure.length+1.0e-7<expected_measure.length)
                    continue;
                bool contains=true;
                for(int sample=0;sample<5&&contains;++sample) {
                    const double parameter=first_parameter+
                        (last_parameter-first_parameter)*sample/4.;
                    const auto probe=BRepBuilderAPI_MakeVertex(
                        expected_curve.Value(parameter)).Vertex();
                    BRepExtrema_DistShapeShape distance(
                        probe,raw_batch_edge_map(index));
                    contains=distance.IsDone()&&distance.NbSolution()>0&&
                        distance.Value()<=1.0e-7;
                }
                if(contains)candidates.push_back(index);
            }
            if(candidates.size()==1) {
                ++missing_edge_global_unique_containment;
                expected_edge_targets[key].insert(candidates.front());
            }
            else if(candidates.empty()) {
                ++missing_edge_global_absent_containment;
                ++unmapped_edges_by_root[root];
            }
            else ++missing_edge_global_ambiguous_containment;
        } else ++missing_edge_raw_ambiguous_containment;
    }
    std::map<std::string,std::vector<TopoDS_Edge>> current_source_edges;
    for(const auto& [edge,reference]:source_edges)
        current_source_edges[reference_key(reference)].push_back(
            TopoDS::Edge(edge));
    TopTools_IndexedDataMapOfShapeListOfShape raw_edge_faces;
    TopExp::MapShapesAndAncestors(
        fuse.Shape(),TopAbs_EDGE,TopAbs_FACE,raw_edge_faces);
    std::vector<std::set<std::string>> raw_edge_face_sources(
        static_cast<std::size_t>(raw_batch_edge_map.Extent())+1);
    for(int edge_index=1;edge_index<=raw_batch_edge_map.Extent();++edge_index) {
        const int ancestor_index=raw_edge_faces.FindIndex(
            raw_batch_edge_map(edge_index));
        if(!ancestor_index)continue;
        for(TopTools_ListIteratorOfListOfShape face(
                raw_edge_faces.FindFromIndex(ancestor_index));face.More();
                face.Next()) {
            const int face_index=raw_batch_face_map.FindIndex(face.Value());
            if(face_index)raw_edge_face_sources[
                static_cast<std::size_t>(edge_index)].insert(
                    raw_batch_face_ancestry[
                        static_cast<std::size_t>(face_index)].begin(),
                    raw_batch_face_ancestry[
                        static_cast<std::size_t>(face_index)].end());
        }
    }
    std::size_t unmapped_root_exact_descendant_sets{};
    for(const auto& [root,count]:unmapped_edges_by_root) {
        std::set<int> descendants;
        const auto sources=current_source_edges.find(root);
        if(sources!=current_source_edges.end())for(const auto& source:sources->second) {
            const auto source_measure=measure_edge(source);
            for(int index=1;index<=raw_batch_edge_map.Extent();++index) {
                const auto& candidate_measure=
                    raw_batch_edge_measures[static_cast<std::size_t>(index)];
                if(candidate_measure.length>source_measure.length+1.0e-7)
                    continue;
                const BRepAdaptor_Curve candidate(
                    TopoDS::Edge(raw_batch_edge_map(index)));
                const double first=candidate.FirstParameter();
                const double last=candidate.LastParameter();
                if(!std::isfinite(first)||!std::isfinite(last))continue;
                bool contained=true;
                for(int sample=0;sample<5&&contained;++sample) {
                    const auto point=BRepBuilderAPI_MakeVertex(
                        candidate.Value(first+(last-first)*sample/4.)).Vertex();
                    BRepExtrema_DistShapeShape distance(point,source);
                    contained=distance.IsDone()&&distance.NbSolution()>0&&
                        distance.Value()<=1.0e-7;
                }
                if(contained)descendants.insert(index);
            }
        }
        std::set<std::string> expected_face_sources;
        const auto root_supports=edge_sets(missing_root_references.at(root));
        for(const auto& encoded:root_supports.first)
            if(const auto reference=decode_face_reference(encoded))
                expected_face_sources.insert(reference_key(*reference));
        std::vector<int> support_candidates;
        for(int index=1;index<=raw_batch_edge_map.Extent();++index)
            if(std::ranges::includes(
                    raw_edge_face_sources[static_cast<std::size_t>(index)],
                    expected_face_sources))support_candidates.push_back(index);
        if(count==1&&support_candidates.size()==1)
            expected_edge_targets[root].insert(
                support_candidates.front());
        unmapped_root_exact_descendant_sets+=descendants.size()==count;
        std::cerr<<"batch unmapped edge root count="<<count
            <<" source_edges="
            <<(sources==current_source_edges.end()?0:sources->second.size())
            <<" current_descendants="<<descendants.size()
            <<" face_sources="<<expected_face_sources.size()
            <<" support_candidates="<<support_candidates.size()
            <<" reconstructed="
            <<reconstructed_batch_edge_targets.contains(root)<<'\n';
    }
    std::set<std::string> expected_vertex_references;
    for(const auto& binding:identity_body(last).kernel_vertices)
        expected_vertex_references.insert(reference_key(binding.reference));
    std::set<std::string> mapped_vertex_references;
    for(std::size_t index=1;index<batch_vertex_ancestry.size();++index)
        mapped_vertex_references.insert(batch_vertex_ancestry[index].begin(),
            batch_vertex_ancestry[index].end());
    std::vector<std::string> missing_vertex_references;
    std::ranges::set_difference(expected_vertex_references,
        mapped_vertex_references,
        std::back_inserter(missing_vertex_references));
    std::map<std::string,TopoDS_Shape> expected_vertex_shapes;
    for(const auto& [vertex,reference]:identity_bound.vertices)
        expected_vertex_shapes.emplace(reference_key(reference),vertex);
    TopTools_IndexedDataMapOfShapeListOfShape identity_vertex_faces,
        identity_vertex_edges,raw_vertex_faces,raw_vertex_edges;
    TopExp::MapShapesAndAncestors(identity_bound.root,TopAbs_VERTEX,
        TopAbs_FACE,identity_vertex_faces);
    TopExp::MapShapesAndAncestors(identity_bound.root,TopAbs_VERTEX,
        TopAbs_EDGE,identity_vertex_edges);
    TopExp::MapShapesAndAncestors(fuse.Shape(),TopAbs_VERTEX,
        TopAbs_FACE,raw_vertex_faces);
    TopExp::MapShapesAndAncestors(fuse.Shape(),TopAbs_VERTEX,
        TopAbs_EDGE,raw_vertex_edges);
    std::size_t missing_vertex_geometry_unique{},
        missing_vertex_geometry_ambiguous{},missing_vertex_geometry_absent{};
    std::size_t missing_vertex_support_unique{},
        missing_vertex_support_ambiguous{},missing_vertex_support_absent{};
    std::size_t moved_vertex_support_unique{},
        moved_vertex_support_ambiguous{},moved_vertex_support_absent{};
    std::size_t missing_vertex_alias_unique{},
        missing_vertex_alias_ambiguous{},missing_vertex_alias_absent{};
    std::size_t moved_vertex_alias_unique{},
        moved_vertex_alias_ambiguous{},moved_vertex_alias_absent{};
    for(const auto& key:missing_vertex_references) {
        const auto expected=expected_vertex_shapes.find(key);
        if(expected==expected_vertex_shapes.end()) {
            ++missing_vertex_geometry_absent;
            ++missing_vertex_support_absent;
            continue;
        }
        const auto expected_point=BRep_Tool::Pnt(
            TopoDS::Vertex(expected->second));
        std::vector<int> geometric_candidates;
        for(int index=1;index<=raw_batch_vertex_map.Extent();++index)
            if(expected_point.Distance(BRep_Tool::Pnt(TopoDS::Vertex(
                    raw_batch_vertex_map(index))))<=1.0e-7)
                geometric_candidates.push_back(index);
        if(geometric_candidates.size()==1)++missing_vertex_geometry_unique;
        else if(geometric_candidates.empty())++missing_vertex_geometry_absent;
        else ++missing_vertex_geometry_ambiguous;
        std::set<std::string> expected_faces,expected_edges;
        const auto collect_expected=[&](const auto& ancestors,
                const auto& values,auto root,auto& result) {
            const int ancestor_index=ancestors.FindIndex(expected->second);
            if(!ancestor_index)return;
            for(TopTools_ListIteratorOfListOfShape ancestor(
                    ancestors.FindFromIndex(ancestor_index));ancestor.More();
                    ancestor.Next())
                for(const auto& [shape,reference]:values)
                    if(shape.IsSame(ancestor.Value()))
                        result.insert(reference_key(root(reference)));
        };
        collect_expected(identity_vertex_faces,identity_bound.faces,
            root_face_reference,expected_faces);
        collect_expected(identity_vertex_edges,identity_bound.edges,
            root_edge_reference,expected_edges);
        std::vector<int> support_candidates;
        std::set<std::string> expected_face_aliases,expected_edge_aliases;
        const auto collect_aliases=[&](const auto& ancestors,
                const auto& values,auto& result) {
            const int ancestor_index=ancestors.FindIndex(expected->second);
            if(!ancestor_index)return;
            for(TopTools_ListIteratorOfListOfShape ancestor(
                    ancestors.FindFromIndex(ancestor_index));ancestor.More();
                    ancestor.Next())
                for(const auto& [shape,reference]:values)
                    if(shape.IsSame(ancestor.Value()))
                        result.insert(reference_key(reference));
        };
        collect_aliases(identity_vertex_faces,identity_bound.faces,
            expected_face_aliases);
        collect_aliases(identity_vertex_edges,identity_bound.edges,
            expected_edge_aliases);
        std::vector<int> alias_candidates;
        for(int index=1;index<=raw_batch_vertex_map.Extent();++index) {
            std::set<std::string> candidate_faces,candidate_edges;
            std::set<int> candidate_face_indices,candidate_edge_indices;
            const int face_index=raw_vertex_faces.FindIndex(
                raw_batch_vertex_map(index));
            if(face_index)for(TopTools_ListIteratorOfListOfShape face(
                    raw_vertex_faces.FindFromIndex(face_index));face.More();
                    face.Next()) {
                const int source=raw_batch_face_map.FindIndex(face.Value());
                if(source) {
                    candidate_face_indices.insert(source);
                    candidate_faces.insert(raw_batch_face_ancestry[
                        static_cast<std::size_t>(source)].begin(),
                        raw_batch_face_ancestry[
                        static_cast<std::size_t>(source)].end());
                }
            }
            const int edge_index=raw_vertex_edges.FindIndex(
                raw_batch_vertex_map(index));
            if(edge_index)for(TopTools_ListIteratorOfListOfShape edge(
                    raw_vertex_edges.FindFromIndex(edge_index));edge.More();
                    edge.Next()) {
                const int source=raw_batch_edge_map.FindIndex(edge.Value());
                if(source) {
                    candidate_edge_indices.insert(source);
                    candidate_edges.insert(raw_batch_edge_ancestry[
                        static_cast<std::size_t>(source)].begin(),
                        raw_batch_edge_ancestry[
                        static_cast<std::size_t>(source)].end());
                }
            }
            if(std::ranges::includes(candidate_faces,expected_faces)&&
               std::ranges::includes(candidate_edges,expected_edges))
                support_candidates.push_back(index);
            const auto aliases_present=[&](const auto& aliases,
                    const auto& targets,const auto& candidate_indices) {
                return std::ranges::all_of(aliases,[&](const auto& alias) {
                    const auto found=targets.find(alias);
                    if(found==targets.end())return false;
                    return std::ranges::any_of(found->second,[&](int target) {
                        return candidate_indices.contains(target);
                    });
                });
            };
            if(aliases_present(expected_face_aliases,
                    expected_face_targets,candidate_face_indices)&&
               aliases_present(expected_edge_aliases,
                    expected_edge_targets,candidate_edge_indices))
                alias_candidates.push_back(index);
        }
        const bool moved=geometric_candidates.empty();
        if(support_candidates.size()==1) {
            ++missing_vertex_support_unique;
            if(moved)++moved_vertex_support_unique;
        } else if(support_candidates.empty()) {
            ++missing_vertex_support_absent;
            if(moved)++moved_vertex_support_absent;
        } else {
            ++missing_vertex_support_ambiguous;
            if(moved)++moved_vertex_support_ambiguous;
        }
        if(alias_candidates.size()==1) {
            ++missing_vertex_alias_unique;
            if(moved)++moved_vertex_alias_unique;
        } else if(alias_candidates.empty()) {
            ++missing_vertex_alias_absent;
            if(moved)++moved_vertex_alias_absent;
        } else {
            ++missing_vertex_alias_ambiguous;
            if(moved)++moved_vertex_alias_ambiguous;
        }
    }
    const auto shape_count=[](const TopoDS_Shape& shape,TopAbs_ShapeEnum kind) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(shape,kind,members);
        return members.Extent();
    };
    std::size_t faces_without_source{},faces_with_aliases{};
    std::size_t alias_count{},maximum_aliases{};
    for(std::size_t index=1;index<batch_face_ancestry.size();++index) {
        const auto count=batch_face_ancestry[index].size();
        if(count==0)++faces_without_source;
        if(count>1) {
            ++faces_with_aliases;
            alias_count+=count-1;
            maximum_aliases=std::max(maximum_aliases,count);
        }
    }
    std::size_t measurement_source_bytes{},measurement_source_face_bindings{};
    for(const auto& snapshot:local_body(last).measurement_sources) {
        measurement_source_bytes+=snapshot->kernel_shape.size();
        measurement_source_face_bindings+=snapshot->kernel_faces.size();
    }
    std::cout<<std::setprecision(17)<<"batch_boolean operation="
        <<(boolean_operation==kernel::BooleanOperation::Add?"add":"subtract")
        <<" first="<<first<<" last="<<last
        <<" tools="<<tools.size()<<" sequential_ms="<<sequential_ms
        <<" tool_ms="<<tool_ms<<" section_ms="<<section_ms
        <<" section_edges="<<section_edges<<" fuse_ms="<<fuse_ms
        <<" spatial_bounds_ms="<<spatial_bounds_ms
        <<" spatial_section_ms="<<spatial_section_ms
        <<" spatial_possible_pairs="<<spatial_possible_pairs
        <<" spatial_candidate_pairs="<<spatial_candidates.size()
        <<" spatial_nonempty_pairs="<<spatial_nonempty_pairs
        <<" spatial_section_curves="<<spatial_section_curves
        <<" spatial_section_points="<<spatial_section_points
        <<" spatial_tangent_pairs="<<spatial_tangent_pairs
        <<" spatial_curves_with_both_pcurves="
        <<spatial_curves_with_both_pcurves
        <<" unify_ms="<<unify_ms<<" archive_ms="<<archive_ms
        <<" publish_ms="<<publish_ms
        <<" published_archive_bytes="<<published.front().kernel_shape.size()
        <<" published_triangles="<<published.front().mesh.triangles.size()/3
        <<" expected_volume="<<expected_properties.Mass()
        <<" batch_volume="<<batch_properties.Mass()
        <<" surface_difference="
        <<std::abs(expected_surface.Mass()-batch_surface.Mass())
        <<" volume_difference="
        <<std::abs(expected_properties.Mass()-batch_properties.Mass())
        <<" expected_minus_batch="<<expected_minus_batch
        <<" batch_minus_expected="<<batch_minus_expected
        <<" expected_solids="<<solids(expected)
        <<" batch_solids="<<solids(unified)
        <<" expected_faces="<<shape_count(expected,TopAbs_FACE)
        <<" batch_faces="<<shape_count(unified,TopAbs_FACE)
        <<" expected_edges="<<shape_count(expected,TopAbs_EDGE)
        <<" batch_edges="<<shape_count(unified,TopAbs_EDGE)
        <<" expected_face_refs="<<expected_face_references.size()
        <<" propagated_face_refs="<<propagated_face_references.size()
        <<" retained_face_refs="<<retained_face_references.size()
        <<" missing_face_refs="<<missing_face_references.size()
        <<" raw_batch_faces="<<raw_batch_face_map.Extent()
        <<" missing_face_raw_unique_containment="
        <<missing_face_raw_unique_containment
        <<" missing_face_raw_ambiguous_containment="
        <<missing_face_raw_ambiguous_containment
        <<" missing_face_raw_absent_containment="
        <<missing_face_raw_absent_containment
        <<" missing_face_fragment_sets="<<missing_face_fragment_sets
        <<" missing_face_fragment_area_matches="
        <<missing_face_fragment_area_matches
        <<" missing_face_root_unique="<<missing_face_root_unique
        <<" missing_face_root_ambiguous="<<missing_face_root_ambiguous
        <<" missing_face_root_absent="<<missing_face_root_absent
        <<" missing_face_root_source_exists="
        <<missing_face_root_source_exists
        <<" raw_face_reference_keys="<<raw_face_reference_keys.size()
        <<" missing_face_boundary_unique="
        <<missing_face_boundary_unique
        <<" missing_face_boundary_tied="<<missing_face_boundary_tied
        <<" missing_face_boundary_empty="<<missing_face_boundary_empty
        <<" expected_edge_refs="<<expected_edge_references.size()
        <<" mapped_edge_refs="<<retained_edge_references.size()
        <<" missing_edge_refs="<<missing_edge_references.size()
        <<" missing_edge_unique_containment="
        <<missing_edge_unique_containment
        <<" missing_edge_ambiguous_containment="
        <<missing_edge_ambiguous_containment
        <<" missing_edge_absent_containment="
        <<missing_edge_absent_containment
        <<" missing_edge_alias_targets="
        <<missing_aliases_per_batch_edge.size()
        <<" raw_batch_edges="<<raw_batch_edge_map.Extent()
        <<" missing_edge_raw_unique_containment="
        <<missing_edge_raw_unique_containment
        <<" missing_edge_raw_ambiguous_containment="
        <<missing_edge_raw_ambiguous_containment
        <<" missing_edge_raw_absent_containment="
        <<missing_edge_raw_absent_containment
        <<" missing_edge_global_unique_containment="
        <<missing_edge_global_unique_containment
        <<" missing_edge_global_ambiguous_containment="
        <<missing_edge_global_ambiguous_containment
        <<" missing_edge_global_absent_containment="
        <<missing_edge_global_absent_containment
        <<" missing_edge_reconstructed_roots="
        <<missing_edge_reconstructed_roots
        <<" missing_edge_root_groups="<<missing_edges_by_root.size()
        <<" unmapped_edge_root_groups="<<unmapped_edges_by_root.size()
        <<" unmapped_root_exact_descendant_sets="
        <<unmapped_root_exact_descendant_sets
        <<" expected_vertex_refs="<<expected_vertex_references.size()
        <<" mapped_vertex_refs="<<mapped_vertex_references.size()
        <<" missing_vertex_refs="<<missing_vertex_references.size()
        <<" missing_vertex_geometry_unique="
        <<missing_vertex_geometry_unique
        <<" missing_vertex_geometry_ambiguous="
        <<missing_vertex_geometry_ambiguous
        <<" missing_vertex_geometry_absent="
        <<missing_vertex_geometry_absent
        <<" missing_vertex_support_unique="
        <<missing_vertex_support_unique
        <<" missing_vertex_support_ambiguous="
        <<missing_vertex_support_ambiguous
        <<" missing_vertex_support_absent="
        <<missing_vertex_support_absent
        <<" moved_vertex_support_unique="
        <<moved_vertex_support_unique
        <<" moved_vertex_support_ambiguous="
        <<moved_vertex_support_ambiguous
        <<" moved_vertex_support_absent="
        <<moved_vertex_support_absent
        <<" missing_vertex_alias_unique="
        <<missing_vertex_alias_unique
        <<" missing_vertex_alias_ambiguous="
        <<missing_vertex_alias_ambiguous
        <<" missing_vertex_alias_absent="
        <<missing_vertex_alias_absent
        <<" moved_vertex_alias_unique="
        <<moved_vertex_alias_unique
        <<" moved_vertex_alias_ambiguous="
        <<moved_vertex_alias_ambiguous
        <<" moved_vertex_alias_absent="
        <<moved_vertex_alias_absent
        <<" faces_without_source="<<faces_without_source
        <<" faces_with_aliases="<<faces_with_aliases
        <<" alias_count="<<alias_count
        <<" maximum_aliases="<<maximum_aliases
        <<" downstream_treatment_edges="<<downstream_treatment_edges.size()
        <<" downstream_treatment_missing="<<downstream_treatment_missing
        <<" downstream_treatment_reconstructed="
        <<downstream_treatment_reconstructed
        <<" downstream_treatment_full="<<downstream_treatment_full
        <<" downstream_treatment_partial="<<downstream_treatment_partial
        <<" downstream_treatment_absent="<<downstream_treatment_absent
        <<" downstream_between_matched="<<downstream_between_matched
        <<" downstream_ends_matched="<<downstream_ends_matched
        <<" downstream_geometric_matches="<<downstream_geometric_matches
        <<" geometric_between_matches="<<geometric_between_matches
        <<" geometric_end_matches="<<geometric_end_matches
        <<" downstream_containment_unique="<<downstream_containment_unique
        <<" containment_target_edges="<<contained_aliases_per_batch_edge.size()
        <<" localized_treatment_ms="<<localized_treatment_ms
        <<" localized_expected_minus="<<localized_expected_minus
        <<" localized_minus_expected="<<localized_minus_expected
        <<" localized_treatment_error=\""<<localized_treatment_error<<'\"'
        <<" downstream_batch_treatment_ms="<<downstream_batch_treatment_ms
        <<" downstream_group_add_ms="<<downstream_group_add_ms
        <<" downstream_group_tool_ms="<<downstream_group_tool_ms
        <<" downstream_group_fuse_ms="<<downstream_group_fuse_ms
        <<" downstream_group_unify_ms="<<downstream_group_unify_ms
        <<" downstream_group_validate_ms="<<downstream_group_validate_ms
        <<" downstream_group_parallel_validate_ms="
        <<downstream_group_parallel_validate_ms
        <<" downstream_replay_last="<<downstream_replay_last
        <<" downstream_batch_expected_minus="
        <<downstream_batch_expected_minus
        <<" downstream_batch_minus_expected="
        <<downstream_batch_minus_expected
        <<" downstream_batch_result_faces="<<downstream_batch_result_faces
        <<" downstream_batch_error="<<std::quoted(downstream_batch_error)
        <<" downstream_subtract_batch_ms="<<downstream_subtract_batch_ms
        <<" downstream_subtract_expected_minus="
        <<downstream_subtract_expected_minus
        <<" downstream_subtract_minus_expected="
        <<downstream_subtract_minus_expected
        <<" downstream_final_add_ms="<<downstream_final_add_ms
        <<" downstream_final_tool_ms="<<downstream_final_tool_ms
        <<" downstream_final_fuse_ms="<<downstream_final_fuse_ms
        <<" downstream_final_unify_ms="<<downstream_final_unify_ms
        <<" downstream_final_validate_ms="<<downstream_final_validate_ms
        <<" downstream_final_parallel_validate_ms="
        <<downstream_final_parallel_validate_ms
        <<" downstream_final_expected_minus="
        <<downstream_final_expected_minus
        <<" downstream_final_minus_expected="
        <<downstream_final_minus_expected
        <<" downstream_treatment_fragments="<<downstream_treatment_fragments
        <<" reconstructable_intersection_edges="
        <<reconstructable_intersection_edges
        <<" unresolved_batch_faces="<<unresolved_batch_faces
        <<" measurement_sources="<<local_body(last).measurement_sources.size()
        <<" measurement_source_bytes="<<measurement_source_bytes
        <<" measurement_source_face_bindings="<<measurement_source_face_bindings
        <<" minimal_reference_count="<<minimal_reference_count
        <<" unique_minimal_references="<<unique_minimal_references.size()
        <<" minimal_reference_bytes="<<minimal_reference_bytes
        <<" minimal_reference_capture_ms="<<minimal_reference_capture_ms
        <<" minimal_tool_ms="<<minimal_tool_ms
        <<" parallel_tool_ms="<<parallel_tool_ms
        <<" hot_tool_ms="<<hot_tool_ms<<'\n';
    return 0;
}

std::string_view primitive_name(const zima::kernel::PrimitiveRequest& primitive) {
    return std::visit([](const auto& value)->std::string_view {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,zima::kernel::ExtrusionRequest>)return "extrusion";
        if constexpr(std::is_same_v<T,zima::kernel::RevolutionRequest>)return "revolution";
        if constexpr(std::is_same_v<T,zima::kernel::FeatureGroupRequest>)return "feature-group";
        if constexpr(std::is_same_v<T,zima::kernel::Sweep3DRequest>)return "sweep3d";
        if constexpr(std::is_same_v<T,zima::kernel::StepRequest>)return "import";
        if constexpr(std::is_same_v<T,zima::kernel::FilletRequest>)return "fillet";
        if constexpr(std::is_same_v<T,zima::kernel::ChamferRequest>)return "chamfer";
        if constexpr(std::is_same_v<T,zima::kernel::ShellRequest>)return "shell";
        if constexpr(std::is_same_v<T,zima::kernel::ThreadSurfaceRequest>)return "thread";
        if constexpr(std::is_same_v<T,zima::kernel::DrillPointRequest>)return "drill-point";
        if constexpr(std::is_same_v<T,zima::kernel::SheetStateRequest>)return "sheet-state";
        if constexpr(std::is_same_v<T,zima::kernel::BoundarySurfaceRequest>)return "boundary-surface";
        if constexpr(std::is_same_v<T,zima::kernel::SolidStateRequest>)return "solid-state";
        if constexpr(std::is_same_v<T,zima::kernel::SurfaceSewingRequest>)return "surface-sewing";
        if constexpr(std::is_same_v<T,zima::kernel::SurfaceIntersectionRequest>)return "surface-intersection";
        if constexpr(std::is_same_v<T,zima::kernel::SurfaceTrimRequest>)return "surface-trim";
        if constexpr(std::is_same_v<T,zima::kernel::SurfaceThickenRequest>)return "surface-thicken";
        if constexpr(std::is_same_v<T,zima::kernel::SheetFormRequest>)return "sheet-form";
        if constexpr(std::is_same_v<T,zima::kernel::LoftRequest>)return "loft";
        return "unknown";
    },primitive);
}

struct BatchEligibility {
    enum class Kind { IndependentAdd, Passthrough, Barrier } kind;
    std::string_view reason;
};

BatchEligibility additive_batch_eligibility(
        const zima::kernel::HistoryOperation& operation) {
    using Kind=BatchEligibility::Kind;
    if(operation.suppressed)return {Kind::Passthrough,"suppressed"};
    if(!operation.input_error.empty())return {Kind::Barrier,"input-error"};
    if(operation.replaces_body)return {Kind::Barrier,"replaces-body"};
    if(operation.sheet_operation!=zima::kernel::SheetOperation::None||
       operation.sheet_material||!operation.sheet_regions.empty())
        return {Kind::Barrier,"sheet-state"};
    if(operation.solid_state_placements||
       !operation.solid_state_face_transfers.empty())
        return {Kind::Barrier,"solid-state"};
    if(operation.operation!=zima::kernel::BooleanOperation::Add)
        return {Kind::Barrier,"subtractive"};
    if(operation.feature_copy) {
        const auto combination=operation.feature_copy->combination;
        if(combination==zima::kernel::BodyCombination::Mirror||
           combination==zima::kernel::BodyCombination::Pattern)
            return {Kind::IndependentAdd,"copied-tool"};
        return {Kind::Barrier,"stateful-copy"};
    }
    const auto* group=std::get_if<zima::kernel::FeatureGroupRequest>(
        &operation.primitive);
    if(!group) {
        if(const auto* loft=std::get_if<zima::kernel::LoftRequest>(
                &operation.primitive);loft&&loft->make_solid)
            return {Kind::IndependentAdd,"independent-loft-tool"};
        return {Kind::Barrier,"stateful-primitive"};
    }
    if(group->children.empty())return {Kind::Passthrough,"reference-only"};
    for(const auto& child:group->children) {
        const auto stateful=std::visit([](const auto& request) {
            using T=std::decay_t<decltype(request)>;
            if constexpr(std::is_same_v<T,zima::kernel::ExtrusionRequest>) {
                return request.extent==zima::kernel::ExtrusionRequest::Extent::ThroughAll||
                    request.through_all_reverse||request.sheet_cut||request.surface_result;
            } else if constexpr(std::is_same_v<T,zima::kernel::RevolutionRequest>) {
                return request.surface_result;
            } else return !request.make_solid;
        },child);
        if(stateful)return {Kind::Barrier,"body-dependent-tool"};
    }
    return {Kind::IndependentAdd,"independent-tool"};
}

int calculation_plan_probe(const std::filesystem::path& source) {
    using Kind=BatchEligibility::Kind;
    const auto document=zima::document::PartDocument::load(source);
    const auto operations=document.kernel_operations(false,true);
    const auto graph=zima::workspace::part_history_dependency_graph(document);
    std::map<std::string,std::vector<std::string>> prerequisites;
    for(const auto& [from,to]:graph.edges)prerequisites[to].push_back(from);
    std::map<std::string,std::string> names;
    for(const auto& feature:document.history)names.emplace(feature.id,feature.name);

    std::size_t batch_count{},batch_operations{},barriers{},passthrough{};
    std::size_t treatment_phase{};
    std::map<std::size_t,std::set<std::string>> phase_treatments;
    std::vector<std::string> operation_order;
    std::map<std::string,std::size_t> operation_indices;
    std::optional<std::size_t> current_batch;
    for(std::size_t index=0;index<operations.size();++index) {
        const auto& operation=operations[index];
        operation_order.push_back(operation.owner_id);
        operation_indices.emplace(operation.owner_id,index);
        const auto eligibility=additive_batch_eligibility(operation);
        const bool edge_treatment=
            std::holds_alternative<zima::kernel::FilletRequest>(
                operation.primitive)||
            std::holds_alternative<zima::kernel::ChamferRequest>(
                operation.primitive);
        if(edge_treatment&&!operation.suppressed&&
           operation.input_error.empty())
            phase_treatments[treatment_phase].insert(operation.owner_id);
        if(eligibility.kind==Kind::IndependentAdd) {
            if(!current_batch)current_batch=batch_count++;
            ++batch_operations;
        } else if(eligibility.kind==Kind::Barrier) {
            current_batch.reset();++barriers;
        } else ++passthrough;
        const auto dependencies=prerequisites.find(operation.owner_id);
        std::cout<<"plan index="<<index<<" owner="<<operation.owner_id
            <<" name=\""<<(names.contains(operation.owner_id)?names.at(operation.owner_id):std::string{})
            <<"\" primitive="<<(operation.feature_copy?"feature-copy":primitive_name(operation.primitive))
            <<" class="<<(eligibility.kind==Kind::IndependentAdd?"batch-add":
                eligibility.kind==Kind::Passthrough?"passthrough":"barrier")
            <<" reason="<<eligibility.reason
            <<" batch="<<(current_batch?std::to_string(*current_batch):"-")
            <<" prerequisites="<<(dependencies==prerequisites.end()?0:dependencies->second.size())
            ;
        if(dependencies!=prerequisites.end()) {
            std::cout<<" prerequisite_owners=";
            for(std::size_t dependency_index=0;
                    dependency_index<dependencies->second.size();
                    ++dependency_index) {
                if(dependency_index)std::cout<<',';
                std::cout<<dependencies->second[dependency_index];
            }
        }
        const auto print_edge_references=[&](const auto& request) {
            std::set<std::string> owners;
            for(const auto& reference:request.edges)
                owners.insert(reference.owner_id);
            std::cout<<" edge_references="<<request.edges.size()
                <<" edge_owners=";
            std::size_t owner_index{};
            for(const auto& owner:owners) {
                if(owner_index++)std::cout<<',';
                std::cout<<owner;
            }
        };
        if(const auto* fillet=std::get_if<zima::kernel::FilletRequest>(
                &operation.primitive))
            print_edge_references(*fillet);
        else if(const auto* chamfer=std::get_if<zima::kernel::ChamferRequest>(
                    &operation.primitive))
            print_edge_references(*chamfer);
        if(const auto* group=std::get_if<zima::kernel::FeatureGroupRequest>(
                &operation.primitive)) {
            std::cout<<" children="<<group->children.size();
            for(std::size_t child_index=0;child_index<group->children.size();
                ++child_index) {
                std::visit([&](const auto& child) {
                    using Child=std::decay_t<decltype(child)>;
                    std::cout<<" child"<<child_index<<'=';
                    if constexpr(std::is_same_v<Child,
                                     zima::kernel::ExtrusionRequest>) {
                        std::cout<<"extrusion(extent="
                            <<static_cast<int>(child.extent)
                            <<",direction="<<child.direction.x<<','
                            <<child.direction.y<<','<<child.direction.z
                            <<",start="<<child.start_offset
                            <<",draft="<<child.draft_angle_degrees
                            <<",regions="<<child.additional_profile_regions.size()+1
                            <<",wall="<<child.wall.has_value()
                            <<",reverse="<<child.reverse_limit.has_value()
                            <<",ordered="<<child.ordered_end_limit.has_value()
                            <<",through-reverse="<<child.through_all_reverse
                            <<')';
                    } else if constexpr(std::is_same_v<Child,
                                            zima::kernel::RevolutionRequest>) {
                        std::cout<<"revolution(angle="<<child.angle_degrees<<')';
                    } else {
                        std::cout<<"sweep(solid="<<child.make_solid<<')';
                    }
                },group->children[child_index]);
            }
        }
        std::cout<<'\n';
        if(!edge_treatment&&eligibility.kind==Kind::Barrier)
            ++treatment_phase;
    }
    for(const auto& [phase,treatments]:phase_treatments) {
        const auto layers=
            zima::workspace::selected_history_dependency_layers(
                operation_order,treatments,graph.edges);
        for(std::size_t layer_index=0;layer_index<layers.size();
                ++layer_index) {
            std::cout<<"treatment_layer phase="<<phase
                <<" wave="<<layer_index<<" operations=";
            for(std::size_t item=0;item<layers[layer_index].size();
                    ++item) {
                if(item)std::cout<<',';
                const auto& owner=layers[layer_index][item];
                std::cout<<operation_indices.at(owner)<<':'<<owner;
            }
            std::cout<<'\n';
        }
    }
    std::cout<<"plan_summary operations="<<operations.size()
        <<" batch_operations="<<batch_operations
        <<" additive_batches="<<batch_count
        <<" barriers="<<barriers
        <<" passthrough="<<passthrough
        <<" sequential_add_booleans="<<batch_operations
        <<" planned_add_booleans="<<batch_count
        <<" avoided_add_booleans="<<(batch_operations-batch_count)<<'\n';
    return 0;
}

int history_cursor_probe(const std::filesystem::path& source) {
    using namespace zima;
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(source,&boundaries);
    workspace::Workspace live;
    const auto document_id=document.document_id;
    const auto body_id=document.body_history.active_body_id();
    const auto* body=document.body_history.find(body_id);
    if(!body)throw std::runtime_error(
        "History cursor probe requires one active Body");
    const auto entry_count=body->entries.size();
    live.add_part(std::move(document),std::move(boundaries),source);
    auto* state=live.open_part(document_id);
    if(!state)throw std::runtime_error(
        "History cursor probe could not open its Part state");
    const auto final_fingerprint=state->session.calculated_boundaries().empty()
        ?std::string{}:state->session.calculated_boundaries().back()
            .source_fingerprint;
    const auto* previous_calculated_allocation=
        state->session.calculated_boundaries().data();
    const std::set<std::size_t> positions{
        0,std::min<std::size_t>(1,entry_count),entry_count/4,
        entry_count/2,(entry_count*3)/4,entry_count};
    double maximum_commit{},maximum_scene{};
    double maximum_revisit_commit{};
    std::size_t materialized_boundaries{};
    kernel::OcctKernel kernel;
    for(const auto position:positions) {
        const auto revision=state->session.revision();
        const auto commit_start=Clock::now();
        const auto change=workspace::set_body_history_cursor(
            live,kernel,document_id,position,body_id);
        const auto commit_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-commit_start).count();
        if(!change.changed||state->session.revision()!=revision+1)
            throw std::runtime_error(
                "History cursor move did not create its metadata transaction");
        materialized_boundaries+=change.boundary_materialized;
        const auto scene_start=Clock::now();
        const auto mesh=state->session.body_context_mesh();
        const auto scene_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-scene_start).count();
        maximum_commit=std::max(maximum_commit,commit_ms);
        maximum_scene=std::max(maximum_scene,scene_ms);
        if(!state->session.calculated_boundaries().empty()&&
           state->session.calculated_boundaries().back().source_fingerprint!=
               final_fingerprint)
            throw std::runtime_error(
                "History cursor move replaced the calculated cache");
        if(!change.boundary_materialized&&
           state->session.calculated_boundaries().data()!=
               previous_calculated_allocation)
            throw std::runtime_error(
                "History cursor move copied the immutable calculated cache");
        if(position&&mesh.triangles.empty())
            throw std::runtime_error(
                "History cursor has no calculated visible body");
        std::cout<<std::setprecision(17)
            <<"history_cursor position="<<position
            <<" entries="<<entry_count
            <<" commit_ms="<<commit_ms
            <<" body_calculated="<<change.boundary_materialized
            <<" scene_ms="<<scene_ms
            <<" vertices="<<mesh.vertices.size()
            <<" triangles="<<mesh.triangles.size()/3
            <<" edges="<<mesh.edges.size()<<'\n';
        previous_calculated_allocation=
            state->session.calculated_boundaries().data();
    }
    if(entry_count!=state->session.document().body_history.find(body_id)->cursor)
        static_cast<void>(workspace::set_body_history_cursor(
            live,kernel,document_id,entry_count,body_id));
    for(const auto position:positions) {
        const auto revisit_start=Clock::now();
        const auto revisit=workspace::set_body_history_cursor(
            live,kernel,document_id,position,body_id);
        const auto revisit_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-revisit_start).count();
        maximum_revisit_commit=std::max(
            maximum_revisit_commit,revisit_ms);
        if(!revisit.changed||revisit.boundary_materialized||
           (position&&!state->session.body_context_mesh().triangles.size()))
            throw std::runtime_error(
                "History cursor revisit recalculated or lost its visible body");
    }
    std::cout<<std::setprecision(17)
        <<"history_cursor_summary maximum_commit_ms="<<maximum_commit
        <<" maximum_scene_ms="<<maximum_scene
        <<" maximum_revisit_commit_ms="<<maximum_revisit_commit
        <<" occt_operations="<<materialized_boundaries<<'\n';
    return 0;
}

int history_cursor_boundary_probe(const std::filesystem::path& source,
        std::size_t position,const std::filesystem::path& destination) {
    using namespace zima;
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(source,&boundaries);
    const auto document_id=document.document_id;
    const auto body_id=document.body_history.active_body_id();
    const auto* body=document.body_history.find(body_id);
    if(!body||position==0||position>=body->entries.size())
        throw std::runtime_error(
            "History boundary probe requires a non-final Body cursor");
    const auto entry_count=body->entries.size();
    // Start from the ordinary final-history state even when the source was
    // saved while rolled back.  The following measured move therefore uses
    // the same production path as Tree > Insert Here without modifying the
    // user's source document.
    document.set_history_cursor(entry_count);
    workspace::Workspace live;
    live.add_part(std::move(document),std::move(boundaries),source);
    auto* state=live.open_part(document_id);
    if(!state)throw std::runtime_error(
        "History boundary probe could not open its Part state");
    kernel::OcctKernel kernel;
    const auto calculate_start=Clock::now();
    const auto change=workspace::set_body_history_cursor(
        live,kernel,document_id,position,body_id);
    const auto calculate_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-calculate_start).count();
    const auto mesh=state->session.body_context_mesh();
    if(!change.changed||mesh.vertices.empty()||mesh.triangles.empty()) {
        std::ostringstream detail;
        detail<<"History boundary probe did not publish its visible Body"
            <<" changed="<<change.changed
            <<" materialized="<<change.boundary_materialized
            <<" vertices="<<mesh.vertices.size()
            <<" triangles="<<mesh.triangles.size();
        throw std::runtime_error(detail.str());
    }

    const auto save_start=Clock::now();
    state->session.document().save(destination,
        state->session.calculated_boundaries());
    const auto save_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-save_start).count();
    const auto reopen_start=Clock::now();
    std::vector<kernel::BodyResult> reopened_boundaries;
    auto reopened=document::PartDocument::load(
        destination,&reopened_boundaries);
    document::DocumentSession reopened_session(
        std::move(reopened),std::move(reopened_boundaries));
    const auto reopened_mesh=reopened_session.body_context_mesh();
    const auto reopen_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-reopen_start).count();
    if(reopened_mesh.vertices.empty()||reopened_mesh.triangles.empty()||
       reopened_mesh.vertices.size()!=mesh.vertices.size()||
       reopened_mesh.triangles.size()!=mesh.triangles.size()||
       reopened_mesh.edges.size()!=mesh.edges.size())
        throw std::runtime_error(
            "History boundary probe changed its visible Body after reopen");

    static_cast<void>(workspace::set_body_history_cursor(
        live,kernel,document_id,entry_count,body_id));
    const auto revisit_start=Clock::now();
    const auto revisit=workspace::set_body_history_cursor(
        live,kernel,document_id,position,body_id);
    const auto revisit_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-revisit_start).count();
    const auto revisit_mesh=state->session.body_context_mesh();
    if(!revisit.changed||revisit.boundary_materialized||
       revisit_mesh.triangles.size()!=mesh.triangles.size())
        throw std::runtime_error(
            "History boundary revisit recalculated or lost its visible Body");
    std::cout<<std::setprecision(17)
        <<"history_cursor_boundary position="<<position
        <<" entries="<<entry_count
        <<" calculate_ms="<<calculate_ms
        <<" save_ms="<<save_ms
        <<" reopen_ms="<<reopen_ms
        <<" revisit_ms="<<revisit_ms
        <<" vertices="<<mesh.vertices.size()
        <<" triangles="<<mesh.triangles.size()/3
        <<" edges="<<mesh.edges.size()<<'\n';
    return 0;
}

int history_cursor_invalidation_probe(const std::filesystem::path& source,
        std::size_t position,const std::filesystem::path& destination) {
    using namespace zima;
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(source,&boundaries);
    const auto document_id=document.document_id;
    const auto body_id=document.body_history.active_body_id();
    const auto* body=document.body_history.find(body_id);
    if(!body||position==0||position>=body->entries.size())
        throw std::runtime_error(
            "History invalidation probe requires a non-final Body cursor");
    const auto entry_count=body->entries.size();
    document.set_history_cursor(entry_count);
    workspace::Workspace live;
    live.add_part(std::move(document),std::move(boundaries),source);
    auto* state=live.open_part(document_id);
    if(!state)throw std::runtime_error(
        "History invalidation probe could not open its Part state");
    body=state->session.document().body_history.find(body_id);
    if(!body)throw std::runtime_error(
        "History invalidation probe lost its active Body");
    kernel::OcctKernel kernel;
    const auto initial=workspace::set_body_history_cursor(
        live,kernel,document_id,position,body_id);
    if(!initial.changed||!initial.boundary_materialized)
        throw std::runtime_error(
            "History invalidation probe could not materialize its baseline");
    body=state->session.document().body_history.find(body_id);
    if(!body)throw std::runtime_error(
        "History invalidation probe lost its materialized Body");
    std::size_t body_operation_count{};
    for(std::size_t entry=0;entry<position;++entry) {
        const auto& row=body->entries[entry];
        if(row.kind!=document::PartHistoryKind::Feature)continue;
        const auto* feature=state->session.document().find_container(row.id);
        if(feature&&feature->feature_kind!=
                document::FeatureKind::Sketch)
            ++body_operation_count;
    }
    const auto before=state->session.calculated_body_boundary(
        body_id,body_operation_count);
    if(!before||before->mesh.triangles.empty())
        throw std::runtime_error(
            "History invalidation baseline has no visible Body");
    static_cast<void>(workspace::set_body_history_cursor(
        live,kernel,document_id,entry_count,body_id));
    body=state->session.document().body_history.find(body_id);
    if(!body)throw std::runtime_error(
        "History invalidation probe lost its restored Body");

    std::optional<document::HistoryContainer> edited;
    double old_length{},new_length{};
    for(std::size_t entry=position;entry>0;--entry) {
        const auto& row=body->entries[entry-1];
        if(row.kind!=document::PartHistoryKind::Feature)continue;
        const auto* feature=state->session.document().find_container(row.id);
        if(!feature||feature->suppressed)continue;
        if(feature->feature_kind==document::FeatureKind::Extrusion&&
           feature->extrusion.end_condition_forward==
                document::EndCondition::Length) {
            edited=*feature;
            old_length=edited->extrusion.length_forward;
            edited->extrusion.length_forward+=std::max(
                0.1,std::abs(old_length)*0.001);
            new_length=edited->extrusion.length_forward;
        } else if(feature->feature_kind==document::FeatureKind::Feature&&
                  feature->feature.type==
                    document::FeatureType::Modeling) {
            for(std::size_t side=0;
                    side<(feature->feature.symmetric?1u:2u);++side) {
                const auto& parameters=feature->feature.sides[side];
                if(parameters.operation!=
                        document::FeatureSideOperation::Extrusion||
                   parameters.extrusion_extent!=
                        document::EndCondition::Length)
                    continue;
                edited=*feature;
                old_length=edited->feature.sides[side].length;
                edited->feature.sides[side].length+=std::max(
                    0.1,std::abs(old_length)*0.001);
                new_length=edited->feature.sides[side].length;
                break;
            }
        }
        if(edited)break;
    }
    if(!edited)throw std::runtime_error(
        "History invalidation probe found no earlier length feature");
    const auto edit_start=Clock::now();
    auto next=state->session.document();
    *next.find_container(edited->id)=*edited;
    auto calculated=workspace::calculate_part_with_resolved_references(
        kernel,next,&state->session.calculated_boundaries());
    state->session.commit(std::move(next),std::move(calculated));
    const auto edit_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-edit_start).count();

    const auto return_start=Clock::now();
    const auto invalidated=workspace::set_body_history_cursor(
        live,kernel,document_id,position,body_id);
    const auto return_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-return_start).count();
    const auto after=state->session.calculated_body_boundary(
        body_id,body_operation_count);
    if(!invalidated.changed||!invalidated.boundary_materialized||
       !after||after->mesh.triangles.empty()||
       after->source_fingerprint==before->source_fingerprint)
        throw std::runtime_error(
            "Earlier dimension change reused a stale history boundary");
    const auto exact_volume=[](const kernel::BodyResult& value) {
        const auto shape=kernel::read_calculated_body_shape(value);
        if(shape.IsNull())throw std::runtime_error(
            "History invalidation boundary has no exact shape");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape,properties);
        return std::abs(properties.Mass());
    };
    const auto before_volume=exact_volume(*before);
    const auto after_volume=exact_volume(*after);
    if(std::abs(after_volume-before_volume)<=1.0e-6)
        throw std::runtime_error(
            "Earlier dimension change did not change exact boundary material");
    state->session.document().save(destination,
        state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> reopened_boundaries;
    auto reopened=document::PartDocument::load(
        destination,&reopened_boundaries);
    document::DocumentSession reopened_session(
        std::move(reopened),std::move(reopened_boundaries));
    if(reopened_session.body_context_mesh().triangles.empty())
        throw std::runtime_error(
            "Invalidated history boundary disappeared after save/reopen");
    std::cout<<std::setprecision(17)
        <<"history_cursor_invalidation position="<<position
        <<" entries="<<entry_count
        <<" edited="<<edited->id
        <<" old_length="<<old_length
        <<" new_length="<<new_length
        <<" edit_ms="<<edit_ms
        <<" invalidated_return_ms="<<return_ms
        <<" before_triangles="<<before->mesh.triangles.size()/3
        <<" after_triangles="<<after->mesh.triangles.size()/3
        <<" before_volume="<<before_volume
        <<" after_volume="<<after_volume
        <<" fingerprint_changed="
            <<(after->source_fingerprint!=before->source_fingerprint)
        <<'\n';
    return 0;
}

int edge_route_probe(const std::filesystem::path& source,
        const std::string& container_id) {
    using namespace zima;
    std::vector<kernel::BodyResult> boundaries;
    auto document=document::PartDocument::load(source,&boundaries);
    document::DocumentSession session(std::move(document),
        std::move(boundaries));
    const auto* container=session.document().find_container(container_id);
    if(!container||
       (container->feature_kind!=document::FeatureKind::Fillet&&
        container->feature_kind!=document::FeatureKind::Chamfer))
        throw std::runtime_error(
            "Edge route probe requires a Fillet or Chamfer container");
    const auto* input=workspace::calculated_operation_input(session,
        container_id);
    if(!input)throw std::runtime_error(
        "Edge route probe has no calculated operation input");
    using VertexKey=std::pair<std::string,std::string>;
    const auto vertex_key=[](const kernel::VertexReference& reference) {
        return VertexKey{reference.owner_id,reference.semantic_key};
    };
    std::map<VertexKey,std::size_t> ambient_degree;
    for(const auto& edge:input->mesh.edges)
        if(!edge.parameter_seam&&
           edge.edge_treatment_endpoint_references.size()==2)
            for(const auto& endpoint:edge.edge_treatment_endpoint_references)
                if(endpoint.valid())++ambient_degree[vertex_key(endpoint)];
    const auto direction=[](const kernel::ViewerEdge& edge,int end)
            ->std::optional<kernel::Vec3> {
        if(edge.points.size()<2)return {};
        const auto& origin=end==0?edge.points.front():edge.points.back();
        for(std::size_t step=1;step<edge.points.size();++step) {
            const auto& target=end==0?edge.points[step]
                :edge.points[edge.points.size()-1-step];
            kernel::Vec3 value{target.x-origin.x,target.y-origin.y,
                target.z-origin.z};
            const double length=std::hypot(std::hypot(value.x,value.y),
                value.z);
            if(length>1.0e-9)return kernel::Vec3{value.x/length,
                value.y/length,value.z/length};
        }
        return {};
    };
    const auto curve_name=[](kernel::OcctEdgeTreatmentCurveKind kind) {
        using Kind=kernel::OcctEdgeTreatmentCurveKind;
        switch(kind) {
        case Kind::Line:return "line";
        case Kind::Circle:return "circle";
        case Kind::Ellipse:return "ellipse";
        case Kind::Hyperbola:return "hyperbola";
        case Kind::Parabola:return "parabola";
        case Kind::Bezier:return "bezier";
        case Kind::BSpline:return "bspline";
        case Kind::Offset:return "offset";
        default:return "other";
        }
    };
    const auto surface_name=[](kernel::OcctEdgeTreatmentSurfaceKind kind) {
        using Kind=kernel::OcctEdgeTreatmentSurfaceKind;
        switch(kind) {
        case Kind::Plane:return "plane";
        case Kind::Cylinder:return "cylinder";
        case Kind::Cone:return "cone";
        case Kind::Sphere:return "sphere";
        case Kind::Torus:return "torus";
        case Kind::Bezier:return "bezier";
        case Kind::BSpline:return "bspline";
        case Kind::Revolution:return "revolution";
        case Kind::Extrusion:return "extrusion";
        case Kind::Offset:return "offset";
        default:return "other";
        }
    };
    kernel::OcctKernel native_kernel;
    std::optional<kernel::BodyResult> recalculated_support_input;
    double support_recalculation_ms{};
    const auto native_support_input=[&]() -> const kernel::BodyResult& {
        if(recalculated_support_input)return *recalculated_support_input;
        const auto operations=session.document().kernel_operations(false,true);
        const auto target=std::ranges::find(operations,container_id,
            &kernel::HistoryOperation::owner_id);
        if(target==operations.end()||target==operations.begin())
            throw std::runtime_error(
                "Edge route probe could not locate its native input prefix");
        const std::vector<kernel::HistoryOperation> prefix(
            operations.begin(),target);
        const auto started=Clock::now();
        kernel::AggregatePropertyScope aggregate_properties(
            kernel::AggregatePropertyDemand::Deferred);
        kernel::FaceMeasurementScope face_measurements(false);
        auto calculated=native_kernel.evaluate_history(prefix);
        support_recalculation_ms=
            std::chrono::duration<double,std::milli>(
                Clock::now()-started).count();
        if(calculated.empty())
            throw std::runtime_error(
                "Edge route probe recalculated an empty native input");
        auto document_result=std::move(calculated.back());
        if(document_result.kernel_shape==kernel::kernel_body_compound) {
            const auto* body=session.document().body_owner_for_object(
                container_id);
            const auto found=body?document_result.body_outputs.find(
                body->scope.id):document_result.body_outputs.end();
            if(!body||found==document_result.body_outputs.end())
                throw std::runtime_error(
                    "Edge route probe recalculated no owning Body output");
            recalculated_support_input=found->second.get();
        } else recalculated_support_input=std::move(document_result);
        return *recalculated_support_input;
    };
    for(std::size_t route_index=0;
            route_index<container->edge_treatment.routes.size();
            ++route_index) {
        const auto& route=container->edge_treatment.routes[route_index];
        std::vector<const kernel::ViewerEdge*> segments;
        std::vector<std::size_t> segment_indices;
        for(std::size_t edge_index=0;
                edge_index<input->mesh.edges.size();++edge_index) {
            const auto& edge=input->mesh.edges[edge_index];
            if(!edge.parameter_seam&&std::ranges::find(route,edge.reference)!=
                    route.end()&&
               edge.edge_treatment_endpoint_references.size()==2) {
                segments.push_back(&edge);
                segment_indices.push_back(edge_index);
            }
        }
        std::optional<kernel::VertexReference> authored_start;
        if(route_index<
                container->edge_treatment.route_start_vertices.size()&&
           container->edge_treatment.route_start_vertices[route_index].valid())
            authored_start=container->edge_treatment.
                route_start_vertices[route_index];
        const auto plan=kernel::build_edge_treatment_route_plan(
            input->mesh.edges,segment_indices,authored_start);
        if(!plan)
            throw std::runtime_error(
                "Persisted treatment route has no deterministic endpoint plan");
        std::vector<kernel::EdgeReference> planned_references;
        planned_references.reserve(plan->segments.size());
        for(const auto& segment:plan->segments)
            planned_references.push_back(
                input->mesh.edges[segment.edge_index].reference);
        kernel::OcctEdgeTreatmentSupportInspection supports;
        bool support_used_recalculated_input{};
        try {
            supports=native_kernel.inspect_edge_treatment_supports(
                *input,planned_references);
        } catch(const std::runtime_error& error) {
            if(std::string_view(error.what())!=
                    "Selected edge is missing from its native Body snapshot")
                throw;
            supports=native_kernel.inspect_edge_treatment_supports(
                native_support_input(),planned_references);
            support_used_recalculated_input=true;
        }
        std::map<kernel::OcctEdgeTreatmentCurveKind,std::size_t>
            curve_counts;
        std::map<std::pair<kernel::OcctEdgeTreatmentSurfaceKind,
            kernel::OcctEdgeTreatmentSurfaceKind>,std::size_t>
            support_counts;
        std::size_t nonmanifold{},missing_face_aliases{},smooth{};
        double smallest_normal_angle=180.,largest_normal_angle{};
        std::size_t normal_angles{};
        for(const auto& segment:supports.segments) {
            ++curve_counts[segment.curve_kind];
            if(segment.adjacent_faces.size()!=2) {
                ++nonmanifold;continue;
            }
            auto pair=std::pair{segment.adjacent_faces[0].kind,
                segment.adjacent_faces[1].kind};
            if(pair.second<pair.first)std::swap(pair.first,pair.second);
            ++support_counts[pair];
            missing_face_aliases+=std::ranges::count_if(
                segment.adjacent_faces,[](const auto& face) {
                    return face.references.empty();
                });
            if(segment.continuity_order)++smooth;
            if(segment.outward_normal_angle_degrees) {
                smallest_normal_angle=std::min(smallest_normal_angle,
                    *segment.outward_normal_angle_degrees);
                largest_normal_angle=std::max(largest_normal_angle,
                    *segment.outward_normal_angle_degrees);
                ++normal_angles;
            }
        }
        std::map<VertexKey,std::vector<std::pair<std::size_t,int>>> vertices;
        for(std::size_t edge_index=0;edge_index<segments.size();++edge_index)
            for(int end=0;end<2;++end) {
                const auto& reference=
                    segments[edge_index]->edge_treatment_endpoint_references[
                        static_cast<std::size_t>(end)];
                if(reference.valid())vertices[vertex_key(reference)].push_back(
                    {edge_index,end});
            }
        double smallest=180.,largest{};std::size_t joints{},branches{};
        for(const auto& [vertex,ends]:vertices) {
            if(ambient_degree[vertex]>2)++branches;
            if(ends.size()!=2)continue;
            const auto first=direction(*segments[ends[0].first],ends[0].second);
            const auto second=direction(*segments[ends[1].first],ends[1].second);
            if(!first||!second)continue;
            const double alignment=first->x*second->x+
                first->y*second->y+first->z*second->z;
            const double turn=std::acos(std::clamp(-alignment,-1.,1.))*
                180./std::numbers::pi;
            smallest=std::min(smallest,turn);largest=std::max(largest,turn);
            ++joints;
            std::cout<<std::setprecision(17)
                <<"edge_route_joint route="<<route_index
                <<" turn_degrees="<<turn
                <<" ambient_degree="<<ambient_degree[vertex]<<'\n';
        }
        std::cout<<std::setprecision(17)
            <<"edge_route route="<<route_index
            <<" persisted_edges="<<route.size()
            <<" geometry_segments="<<segments.size()
            <<" joints="<<joints
            <<" free_ends="<<std::ranges::count_if(vertices,
                [](const auto& value){return value.second.size()==1;})
            <<" branch_vertices="<<branches
            <<" planned_segments="<<plan->segments.size()
            <<" planned_closed="<<plan->closed
            <<" planned_terminals="<<plan->terminals.size()
            <<" planned_start_body_incidence="
            <<(plan->terminals.empty()?0:
                plan->terminals.front().body_incidence)
            <<" planned_end_body_incidence="
            <<(plan->terminals.empty()?0:
                plan->terminals.back().body_incidence)
            <<" smallest_turn_degrees="<<(joints?smallest:-1.)
            <<" largest_turn_degrees="<<(joints?largest:-1.)
            <<" support_decode_ms="<<supports.native_decode_ms
            <<" support_adjacency_ms="<<supports.adjacency_ms
            <<" support_recalculated_input="
            <<support_used_recalculated_input
            <<" support_recalculation_ms="
            <<(support_used_recalculated_input?
                support_recalculation_ms:0.)
            <<" support_nonmanifold="<<nonmanifold
            <<" support_missing_aliases="<<missing_face_aliases
            <<" support_smooth="<<smooth
            <<" support_smallest_normal_angle_degrees="
            <<(normal_angles?smallest_normal_angle:-1.)
            <<" support_largest_normal_angle_degrees="
            <<(normal_angles?largest_normal_angle:-1.)<<'\n';
        for(const auto& [kind,count]:curve_counts)
            std::cout<<"edge_route_curve route="<<route_index
                <<" kind="<<curve_name(kind)<<" count="<<count<<'\n';
        for(const auto& [pair,count]:support_counts)
            std::cout<<"edge_route_support route="<<route_index
                <<" first="<<surface_name(pair.first)
                <<" second="<<surface_name(pair.second)
                <<" count="<<count<<'\n';
        for(std::size_t segment_index=0;
                segment_index<supports.segments.size();++segment_index) {
            const auto& segment=supports.segments[segment_index];
            std::cout<<"edge_route_segment route="<<route_index
                <<" segment="<<segment_index
                <<" edge_owner="<<segment.edge.owner_id
                <<" curve="<<curve_name(segment.curve_kind)
                <<" continuity="<<segment.continuity_order<<'\n';
            for(std::size_t face_index=0;
                    face_index<segment.adjacent_faces.size();++face_index) {
                const auto& face=segment.adjacent_faces[face_index];
                std::cout<<"edge_route_face route="<<route_index
                    <<" segment="<<segment_index
                    <<" face="<<face_index
                    <<" kind="<<surface_name(face.kind)
                    <<" reversed="<<face.reversed;
                for(const auto& reference:face.references)
                    std::cout<<" alias_owner="<<reference.owner_id
                        <<" alias_key="<<reference.semantic_key;
                std::cout<<'\n';
            }
        }
    }
    return 0;
}

int sequential_range_probe(const std::filesystem::path& source,
        std::size_t first,std::size_t last) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    const auto document=document::PartDocument::load(source,&previous);
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto operations=document.kernel_operations(false,true);
    if(first==0||first>last||last>=operations.size()||last>=previous.size())
        throw std::runtime_error("Sequential range is outside calculated history");
    const std::vector<kernel::HistoryOperation> requested(
        operations.begin(),operations.begin()+static_cast<std::ptrdiff_t>(last+1));
    const std::vector<kernel::BodyResult> prefix(
        previous.begin(),previous.begin()+static_cast<std::ptrdiff_t>(first));
    kernel::OcctKernel kernel;
    const auto start=Clock::now();
    const auto calculated=kernel.evaluate_history_incremental(requested,prefix);
    const auto elapsed=std::chrono::duration<double,std::milli>(
        Clock::now()-start).count();
    if(calculated.size()!=last+1||!calculated.back().calculation_errors.empty())
        throw std::runtime_error("Sequential range calculation failed");
    std::cout<<"sequential_range first="<<first<<" last="<<last
        <<" milliseconds="<<elapsed
        <<" properties_current="<<calculated.back().physical_properties_current
        <<'\n';
    return 0;
}

int append_last_operation_probe(const std::filesystem::path& source,
        bool verify_full_replay=false,
        bool redundant_geometry=false) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    const auto document=document::PartDocument::load(source,&previous);
    auto operations=document.kernel_operations(false,true);
    if(operations.empty()||previous.empty())
        throw std::runtime_error(
            "Append probe requires a calculated Part history");
    auto appended=operations.back();
    appended.owner_id += ":append-probe";
    appended.suppressed=false;
    if(auto* group=std::get_if<kernel::FeatureGroupRequest>(
            &appended.primitive))
        for(auto& child:group->children)
            if(auto* extrusion=std::get_if<kernel::ExtrusionRequest>(
                    &child)) {
                extrusion->extent=
                    kernel::ExtrusionRequest::Extent::Blind;
                extrusion->target_face={};
                extrusion->target_is_datum=false;
                extrusion->target_surface_triangles.clear();
                extrusion->reverse_limit.reset();
                extrusion->ordered_end_limit.reset();
                // Exercise an actual appended feature by extending the copied
                // tool beyond its preceding material. An exact duplicate is a
                // separate, deliberately pathological Boolean-provenance case:
                // OCCT may report different parent histories for coincident
                // operands depending on whether they are fused in one batch or
                // sequentially.
                if(!redundant_geometry) {
                    extrusion->direction.x*=1.125;
                    extrusion->direction.y*=1.125;
                    extrusion->direction.z*=1.125;
                }
            }
    operations.push_back(std::move(appended));
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    kernel::HistoryOutputPolicy output_policy;
    output_policy.final_boundary_first=true;
    kernel::OcctKernel kernel;
    const auto start=Clock::now();
    const auto calculated=kernel.evaluate_history_recovering(
        operations,previous,output_policy);
    const auto elapsed=std::chrono::duration<double,std::milli>(
        Clock::now()-start).count();
    if(calculated.size()!=operations.size()||
       !calculated.back().calculation_errors.empty())
        throw std::runtime_error("Appended operation calculation failed");
    const auto shape=kernel::read_calculated_body_shape(
        calculated.back());
    if(shape.IsNull()||
       !BRepCheck_Analyzer(shape,true,true,false).IsValid())
        throw std::runtime_error(
            "Appended operation produced an invalid final body");
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape,TopAbs_FACE,faces);
    double full_replay_ms=-1.;
    double incremental_minus_full=-1.;
    double full_minus_incremental=-1.;
    bool identity_equal{};
    if(verify_full_replay) {
        kernel::OcctKernel full_kernel;
        const auto full_start=Clock::now();
        const auto full=full_kernel.evaluate_history_recovering(
            operations,{},output_policy);
        full_replay_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-full_start).count();
        if(full.size()!=operations.size()||
           !full.back().calculation_errors.empty())
            throw std::runtime_error(
                "Full append reference calculation failed");
        const auto full_shape=kernel::read_calculated_body_shape(
            full.back());
        const auto material_difference=[](const TopoDS_Shape& left,
                const TopoDS_Shape& right) {
            BRepAlgoAPI_Cut cut(left,right);
            cut.SetRunParallel(true);
            cut.SetFuzzyValue(1.0e-7);
            cut.Build();
            if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
            GProp_GProps properties;
            BRepGProp::VolumeProperties(cut.Shape(),properties);
            return std::abs(properties.Mass());
        };
        incremental_minus_full=material_difference(shape,full_shape);
        full_minus_incremental=material_difference(full_shape,shape);
        const auto references=[](const kernel::BodyResult& value) {
            std::set<std::string> result;
            const auto append=[&](const auto& bindings) {
                for(const auto& binding:bindings)
                    result.insert(binding.reference.owner_id+'\x1f'+
                        binding.reference.semantic_key+'\x1f'+
                        binding.reference.instance_path);
            };
            append(value.kernel_faces);append(value.kernel_edges);
            append(value.kernel_vertices);
            for(const auto& [id,output]:value.body_outputs) {
                static_cast<void>(id);
                append(output->kernel_faces);append(output->kernel_edges);
                append(output->kernel_vertices);
            }
            return result;
        };
        const auto incremental_references=references(calculated.back());
        const auto full_references=references(full.back());
        identity_equal=incremental_references==full_references;
        if(incremental_minus_full<0||full_minus_incremental<0||
           incremental_minus_full>1.0e-6||
           full_minus_incremental>1.0e-6||!identity_equal) {
            std::vector<std::string> missing,added;
            std::ranges::set_difference(incremental_references,
                full_references,std::back_inserter(added));
            std::ranges::set_difference(full_references,
                incremental_references,std::back_inserter(missing));
            std::cerr<<std::setprecision(17)
                <<"append equivalence incremental_minus_full="
                <<incremental_minus_full
                <<" full_minus_incremental="
                <<full_minus_incremental
                <<" incremental_references="
                <<incremental_references.size()
                <<" full_references="<<full_references.size()
                <<" missing="<<missing.size()
                <<" added="<<added.size()<<'\n';
            for(std::size_t index=0;index<std::min<std::size_t>(
                    10,missing.size());++index)
                std::cerr<<"missing_reference="<<missing[index]<<'\n';
            for(std::size_t index=0;index<std::min<std::size_t>(
                    10,added.size());++index)
                std::cerr<<"added_reference="<<added[index]<<'\n';
            throw std::runtime_error(
                "Incremental append changed material or stable identities");
        }
    }
    std::cout<<std::setprecision(17)
        <<"append_last_operation previous="<<previous.size()
        <<" operations="<<operations.size()
        <<" milliseconds="<<elapsed
        <<" faces="<<faces.Extent()
        <<" properties_current="
        <<calculated.back().physical_properties_current
        <<" full_replay_ms="<<full_replay_ms
        <<" incremental_minus_full="<<incremental_minus_full
        <<" full_minus_incremental="<<full_minus_incremental
        <<" identity_equal="<<identity_equal<<'\n';
    return 0;
}

int direct_regeneration_prefix_probe(
        const std::filesystem::path& source,std::size_t count) {
    using namespace zima;
    const auto document=document::PartDocument::load(source);
    auto operations=document.kernel_operations(false,true);
    if(count<2||count>operations.size())
        throw std::runtime_error(
            "Direct regeneration prefix is outside Part history");
    operations.resize(count);
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto calculate=[&](bool direct) {
        kernel::OcctKernel geometry;
        const auto start=Clock::now();
        std::vector<kernel::BodyResult> result;
        if(direct) {
            kernel::HistoryOutputPolicy policy;
            policy.final_boundary_first=true;
            result=geometry.evaluate_history_recovering(
                operations,{},policy);
        } else result=geometry.evaluate_history(operations);
        const auto elapsed=std::chrono::duration<double,std::milli>(
            Clock::now()-start).count();
        if(result.size()!=operations.size()||
           !result.back().calculation_errors.empty())
            throw std::runtime_error(
                "Direct regeneration prefix calculation failed");
        return std::pair{std::move(result),elapsed};
    };
    auto [serial,serial_ms]=calculate(false);
    if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
        std::cerr<<"direct_prefix serial_complete_ms="<<serial_ms<<'\n';
    auto [direct,direct_ms]=calculate(true);
    if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))
        std::cerr<<"direct_prefix direct_complete_ms="<<direct_ms<<'\n';
    const auto serial_shape=kernel::read_calculated_body_shape(
        serial.back());
    const auto direct_shape=kernel::read_calculated_body_shape(
        direct.back());
    const auto difference=[](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left,right);
        cut.SetRunParallel(true);
        cut.SetFuzzyValue(1.0e-7);
        cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const bool skip_shape_difference=
        std::getenv("ZIMA_CPP_SKIP_SHAPE_DIFFERENCE")!=nullptr;
    const auto direct_minus_serial=skip_shape_difference?0.:
        difference(direct_shape,serial_shape);
    const auto serial_minus_direct=skip_shape_difference?0.:
        difference(serial_shape,direct_shape);
    const auto references=[](const kernel::BodyResult& value) {
        std::set<std::string> result;
        const auto append=[&](const auto& bindings) {
            for(const auto& binding:bindings)
                result.insert(binding.reference.owner_id+'\x1f'+
                    binding.reference.semantic_key+'\x1f'+
                    binding.reference.instance_path);
        };
        const auto collect=[&](const auto& self,
                const kernel::BodyResult& body)->void {
            append(body.kernel_faces);append(body.kernel_edges);
            append(body.kernel_vertices);
            for(const auto& [id,output]:body.body_outputs) {
                static_cast<void>(id);
                self(self,output.get());
            }
        };
        collect(collect,value);
        return result;
    };
    const auto serial_references=references(serial.back());
    const auto direct_references=references(direct.back());
    TopTools_IndexedMapOfShape serial_faces,direct_faces,
        serial_edges,direct_edges,serial_vertices,direct_vertices;
    TopExp::MapShapes(serial_shape,TopAbs_FACE,serial_faces);
    TopExp::MapShapes(direct_shape,TopAbs_FACE,direct_faces);
    TopExp::MapShapes(serial_shape,TopAbs_EDGE,serial_edges);
    TopExp::MapShapes(direct_shape,TopAbs_EDGE,direct_edges);
    TopExp::MapShapes(serial_shape,TopAbs_VERTEX,serial_vertices);
    TopExp::MapShapes(direct_shape,TopAbs_VERTEX,direct_vertices);
    std::cout<<std::setprecision(17)
        <<"direct_regeneration_prefix count="<<count
        <<" serial_ms="<<serial_ms
        <<" direct_ms="<<direct_ms
        <<" serial_faces="<<serial_faces.Extent()
        <<" direct_faces="<<direct_faces.Extent()
        <<" serial_edges="<<serial_edges.Extent()
        <<" direct_edges="<<direct_edges.Extent()
        <<" serial_vertices="<<serial_vertices.Extent()
        <<" direct_vertices="<<direct_vertices.Extent()
        <<" direct_minus_serial="<<direct_minus_serial
        <<" serial_minus_direct="<<serial_minus_direct
        <<" serial_references="<<serial_references.size()
        <<" direct_references="<<direct_references.size()
        <<" identity_equal="
        <<(serial_references==direct_references)<<'\n';
    if(direct_minus_serial<0||serial_minus_direct<0||
       direct_minus_serial>1.0e-6||serial_minus_direct>1.0e-6)
        return 2;
    return serial_references==direct_references?0:3;
}

int planar_fillet_equivalence_probe(
        const std::filesystem::path& source,std::size_t count) {
    using namespace zima;
    const auto document=document::PartDocument::load(source);
    auto operations=document.kernel_operations(false,true);
    if(count<2||count>operations.size())
        throw std::runtime_error(
            "Planar Fillet equivalence prefix is outside Part history");
    operations.resize(count);
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto calculate=[&](bool custom) {
        EnvironmentVariableScope environment(
            "ZIMA_CPP_EXPERIMENTAL_PLANAR_FILLET",custom?"1":nullptr);
        kernel::OcctKernel geometry;
        const auto start=Clock::now();
        auto result=geometry.evaluate_history(operations);
        const double elapsed=std::chrono::duration<double,std::milli>(
            Clock::now()-start).count();
        if(result.size()!=operations.size()||
           !result.back().calculation_errors.empty())
            throw std::runtime_error(
                "Planar Fillet equivalence calculation failed");
        return std::pair{std::move(result.back()),elapsed};
    };
    auto [reference,reference_ms]=calculate(false);
    auto [custom,custom_ms]=calculate(true);
    const auto reference_shape=kernel::read_calculated_body_shape(reference);
    const auto custom_shape=kernel::read_calculated_body_shape(custom);
    const auto difference=[](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left,right);
        cut.SetRunParallel(false);cut.SetFuzzyValue(1.0e-7);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto references=[](const kernel::BodyResult& body) {
        std::set<std::string> result;
        const auto append=[&](const auto& values) {
            for(const auto& value:values)
                result.insert(value.reference.owner_id+'\x1f'+
                    value.reference.semantic_key+'\x1f'+
                    value.reference.instance_path);
        };
        const auto collect=[&](const auto& self,
                const kernel::BodyResult& value)->void {
            append(value.kernel_faces);append(value.kernel_edges);
            append(value.kernel_vertices);
            for(const auto& [id,output]:value.body_outputs) {
                static_cast<void>(id);self(self,output.get());
            }
        };
        collect(collect,body);return result;
    };
    const auto face_kinds=[](const TopoDS_Shape& shape) {
        std::array<std::size_t,3> result{};
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(shape,TopAbs_FACE,faces);
        for(int index=1;index<=faces.Extent();++index) {
            switch(BRepAdaptor_Surface(
                    TopoDS::Face(faces.FindKey(index)),true).GetType()) {
            case GeomAbs_Plane:++result[0];break;
            case GeomAbs_Cylinder:++result[1];break;
            default:++result[2];break;
            }
        }
        return result;
    };
    const auto topology_counts=[](const TopoDS_Shape& shape) {
        std::array<std::size_t,2> result{};
        TopTools_IndexedMapOfShape edges,vertices;
        TopExp::MapShapes(shape,TopAbs_EDGE,edges);
        TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);
        result[0]=static_cast<std::size_t>(edges.Extent());
        result[1]=static_cast<std::size_t>(vertices.Extent());
        return result;
    };
    const auto reference_key=[](const auto& reference) {
        return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+
            reference.instance_path;
    };
    const auto binding_multiplicity=[&](const kernel::BodyResult& body) {
        std::map<std::string,std::size_t> result;
        for(const auto& binding:body.kernel_edges)
            ++result[reference_key(binding.reference)];
        return result;
    };
    struct ViewerWireStats {
        std::size_t edges{},segments{},treatment_edges{},treatment_segments{};
        std::map<std::string,std::size_t> references;
        std::size_t maximum_reference_fragments{};
    };
    const auto viewer_wire_stats=[&](const kernel::BodyResult& body,
            std::string_view treatment_owner) {
        ViewerWireStats result;
        for(const auto& edge:body.mesh.edges) {
            if(edge.parameter_seam||edge.points.size()<2)continue;
            ++result.edges;
            result.segments+=edge.points.size()-1;
            if(edge.reference.valid())
                ++result.references[reference_key(edge.reference)];
            if(std::ranges::find(edge.edge_treatment_owner_ids,
                    treatment_owner)!=edge.edge_treatment_owner_ids.end()) {
                ++result.treatment_edges;
                result.treatment_segments+=edge.points.size()-1;
            }
        }
        for(const auto& [key,count]:result.references) {
            static_cast<void>(key);
            result.maximum_reference_fragments=std::max(
                result.maximum_reference_fragments,count);
        }
        return result;
    };
    const auto reference_references=references(reference);
    const auto custom_references=references(custom);
    const auto reference_faces=face_kinds(reference_shape);
    const auto custom_faces=face_kinds(custom_shape);
    const auto reference_topology=topology_counts(reference_shape);
    const auto custom_topology=topology_counts(custom_shape);
    const auto reference_bindings=binding_multiplicity(reference);
    const auto custom_bindings=binding_multiplicity(custom);
    const auto reference_wire=viewer_wire_stats(reference,
        operations.back().owner_id);
    const auto custom_wire=viewer_wire_stats(custom,
        operations.back().owner_id);
    const double custom_minus_reference=difference(
        custom_shape,reference_shape);
    const double reference_minus_custom=difference(
        reference_shape,custom_shape);
    std::cout<<std::setprecision(17)
        <<"planar_fillet_equivalence count="<<count
        <<" reference_ms="<<reference_ms
        <<" custom_ms="<<custom_ms
        <<" custom_minus_reference="<<custom_minus_reference
        <<" reference_minus_custom="<<reference_minus_custom
        <<" reference_faces="
        <<reference_faces[0]+reference_faces[1]+reference_faces[2]
        <<" custom_faces="
        <<custom_faces[0]+custom_faces[1]+custom_faces[2]
        <<" reference_plane_faces="<<reference_faces[0]
        <<" custom_plane_faces="<<custom_faces[0]
        <<" reference_cylinder_faces="<<reference_faces[1]
        <<" custom_cylinder_faces="<<custom_faces[1]
        <<" reference_other_faces="<<reference_faces[2]
        <<" custom_other_faces="<<custom_faces[2]
        <<" reference_edges="<<reference_topology[0]
        <<" custom_edges="<<custom_topology[0]
        <<" reference_vertices="<<reference_topology[1]
        <<" custom_vertices="<<custom_topology[1]
        <<" reference_references="<<reference_references.size()
        <<" custom_references="<<custom_references.size()
        <<" identity_equal="
        <<(reference_references==custom_references)
        <<" edge_binding_multiplicity_equal="
        <<(reference_bindings==custom_bindings)
        <<" reference_visible_edges="<<reference_wire.edges
        <<" custom_visible_edges="<<custom_wire.edges
        <<" reference_visible_segments="<<reference_wire.segments
        <<" custom_visible_segments="<<custom_wire.segments
        <<" reference_treatment_wire_edges="
        <<reference_wire.treatment_edges
        <<" custom_treatment_wire_edges="<<custom_wire.treatment_edges
        <<" reference_treatment_wire_segments="
        <<reference_wire.treatment_segments
        <<" custom_treatment_wire_segments="
        <<custom_wire.treatment_segments
        <<" reference_maximum_reference_fragments="
        <<reference_wire.maximum_reference_fragments
        <<" custom_maximum_reference_fragments="
        <<custom_wire.maximum_reference_fragments
        <<" viewer_reference_multiplicity_equal="
        <<(reference_wire.references==custom_wire.references)<<'\n';
    if(const auto* fillet=std::get_if<kernel::FilletRequest>(
            &operations.back().primitive);
       fillet&&fillet->mode==kernel::FilletRequest::Mode::Constant&&
       fillet->edges.size()>1&&count<=4) {
        const auto complete=*fillet;
        const auto cylinder_stats=[](const TopoDS_Shape& shape) {
            std::array<double,3> result{};
            for(TopExp_Explorer item(shape,TopAbs_FACE);
                    item.More();item.Next()) {
                const auto face=TopoDS::Face(item.Current());
                const BRepAdaptor_Surface support(face,true);
                if(support.GetType()!=GeomAbs_Cylinder)continue;
                double u_min{},u_max{},v_min{},v_max{};
                BRepTools::UVBounds(face,u_min,u_max,v_min,v_max);
                GProp_GProps properties;
                BRepGProp::SurfaceProperties(face,properties);
                result={support.Cylinder().Radius(),v_max-v_min,
                    std::abs(properties.Mass())};
                break;
            }
            return result;
        };
        for(std::size_t index=0;index<complete.edges.size();++index) {
            auto single=complete;
            single.edges={complete.edges[index]};
            operations.back().primitive=single;
            auto [single_reference,single_reference_ms]=calculate(false);
            auto [single_custom,single_custom_ms]=calculate(true);
            const auto single_reference_shape=
                kernel::read_calculated_body_shape(single_reference);
            const auto single_custom_shape=
                kernel::read_calculated_body_shape(single_custom);
            TopTools_IndexedMapOfShape single_reference_faces,
                single_custom_faces;
            TopExp::MapShapes(single_reference_shape,TopAbs_FACE,
                single_reference_faces);
            TopExp::MapShapes(single_custom_shape,TopAbs_FACE,
                single_custom_faces);
            const auto reference_cylinder=
                cylinder_stats(single_reference_shape);
            const auto custom_cylinder=cylinder_stats(single_custom_shape);
            std::cout<<std::setprecision(17)
                <<"planar_fillet_edge index="<<index
                <<" owner="<<complete.edges[index].owner_id
                <<" key="<<complete.edges[index].semantic_key
                <<" reference_ms="<<single_reference_ms
                <<" custom_ms="<<single_custom_ms
                <<" custom_minus_reference="
                <<difference(single_custom_shape,single_reference_shape)
                <<" reference_minus_custom="
                <<difference(single_reference_shape,single_custom_shape)
                <<" reference_faces="<<single_reference_faces.Extent()
                <<" custom_faces="<<single_custom_faces.Extent()
                <<" radius="<<reference_cylinder[0]
                <<" reference_cylinder_v_span="<<reference_cylinder[1]
                <<" custom_cylinder_v_span="<<custom_cylinder[1]
                <<" reference_cylinder_area="<<reference_cylinder[2]
                <<" custom_cylinder_area="<<custom_cylinder[2]<<'\n';
        }
        operations.back().primitive=complete;
    }
    if(custom_minus_reference<0||reference_minus_custom<0||
       custom_minus_reference>1.0e-6||reference_minus_custom>1.0e-6)
        return 2;
    if(reference_references!=custom_references)return 3;
    return reference_topology==custom_topology&&
           reference_bindings==custom_bindings&&
           reference_wire.references==custom_wire.references?0:4;
}

int planar_fillet_batch_probe(const std::filesystem::path& source,
        std::size_t first_index) {
    using namespace zima;
    const auto document=document::PartDocument::load(source);
    auto sequential=document.kernel_operations(false,true);
    if(first_index+1>=sequential.size())
        throw std::runtime_error(
            "Planar Fillet batch range is outside Part history");
    sequential.resize(first_index+2);
    const auto* first=std::get_if<kernel::FilletRequest>(
        &sequential[first_index].primitive);
    const auto* second=std::get_if<kernel::FilletRequest>(
        &sequential[first_index+1].primitive);
    if(!first||!second||
       first->mode!=kernel::FilletRequest::Mode::Constant||
       second->mode!=kernel::FilletRequest::Mode::Constant||
       std::abs(first->radius_start-second->radius_start)>1.0e-12)
        throw std::runtime_error(
            "Planar Fillet batch requires two equal constant-radius Fillets");
    auto merged=sequential;
    auto merged_request=*first;
    merged_request.edges.insert(merged_request.edges.end(),
        second->edges.begin(),second->edges.end());
    merged[first_index].primitive=std::move(merged_request);
    merged.erase(merged.begin()+static_cast<std::ptrdiff_t>(first_index+1));
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto calculate=[](const auto& operations,bool custom) {
        EnvironmentVariableScope environment(
            "ZIMA_CPP_EXPERIMENTAL_PLANAR_FILLET",custom?"1":nullptr);
        kernel::OcctKernel geometry;
        const auto start=Clock::now();
        auto results=geometry.evaluate_history(operations);
        const double elapsed=std::chrono::duration<double,std::milli>(
            Clock::now()-start).count();
        if(results.size()!=operations.size()||
           !results.back().calculation_errors.empty())
            throw std::runtime_error(
                "Planar Fillet batch calculation failed");
        return std::pair{std::move(results.back()),elapsed};
    };
    auto [reference,reference_ms]=calculate(sequential,false);
    auto [custom_sequential,custom_sequential_ms]=
        calculate(sequential,true);
    auto [occt_merged,occt_merged_ms]=calculate(merged,false);
    auto [custom_merged,custom_merged_ms]=calculate(merged,true);
    const auto reference_shape=kernel::read_calculated_body_shape(reference);
    const auto custom_sequential_shape=
        kernel::read_calculated_body_shape(custom_sequential);
    const auto occt_merged_shape=
        kernel::read_calculated_body_shape(occt_merged);
    const auto custom_merged_shape=
        kernel::read_calculated_body_shape(custom_merged);
    const auto difference=[](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left,right);
        cut.SetRunParallel(false);cut.SetFuzzyValue(1.0e-7);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto face_count=[](const TopoDS_Shape& shape) {
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(shape,TopAbs_FACE,faces);
        return faces.Extent();
    };
    const double custom_sequential_minus_reference=difference(
        custom_sequential_shape,reference_shape);
    const double reference_minus_custom_sequential=difference(
        reference_shape,custom_sequential_shape);
    const double occt_merged_minus_reference=difference(
        occt_merged_shape,reference_shape);
    const double reference_minus_occt_merged=difference(
        reference_shape,occt_merged_shape);
    const double custom_merged_minus_reference=difference(
        custom_merged_shape,reference_shape);
    const double reference_minus_custom_merged=difference(
        reference_shape,custom_merged_shape);
    std::cout<<std::setprecision(17)
        <<"planar_fillet_batch first_index="<<first_index
        <<" reference_ms="<<reference_ms
        <<" custom_sequential_ms="<<custom_sequential_ms
        <<" occt_merged_ms="<<occt_merged_ms
        <<" custom_merged_ms="<<custom_merged_ms
        <<" reference_faces="<<face_count(reference_shape)
        <<" custom_sequential_faces="
        <<face_count(custom_sequential_shape)
        <<" occt_merged_faces="<<face_count(occt_merged_shape)
        <<" custom_merged_faces="<<face_count(custom_merged_shape)
        <<" custom_sequential_minus_reference="
        <<custom_sequential_minus_reference
        <<" reference_minus_custom_sequential="
        <<reference_minus_custom_sequential
        <<" occt_merged_minus_reference="
        <<occt_merged_minus_reference
        <<" reference_minus_occt_merged="
        <<reference_minus_occt_merged
        <<" custom_merged_minus_reference="
        <<custom_merged_minus_reference
        <<" reference_minus_custom_merged="
        <<reference_minus_custom_merged<<'\n';
    const auto equivalent=[](double first_difference,
            double second_difference) {
        return first_difference>=0&&second_difference>=0&&
            first_difference<=1.0e-6&&second_difference<=1.0e-6;
    };
    return equivalent(custom_sequential_minus_reference,
               reference_minus_custom_sequential)&&
           equivalent(occt_merged_minus_reference,
               reference_minus_occt_merged)&&
           equivalent(custom_merged_minus_reference,
               reference_minus_custom_merged)?0:2;
}

int primary_region_treatment_probe(const std::filesystem::path& source) {
    using namespace zima;
    const auto document=document::PartDocument::load(source);
    const auto operations=document.kernel_operations(false,true);
    const auto first_treatment=std::ranges::find_if(operations,
        [](const auto& operation) {
            return std::holds_alternative<kernel::FilletRequest>(
                operation.primitive);
        });
    if(first_treatment==operations.end()||first_treatment==operations.begin())
        throw std::runtime_error(
            "Primary-region probe requires additive sources before Fillet");
    const auto source_count=static_cast<std::size_t>(
        first_treatment-operations.begin());
    std::map<std::string,std::size_t> source_by_owner;
    for(std::size_t index=0;index<source_count;++index) {
        if(operations[index].suppressed||
           operations[index].operation!=kernel::BooleanOperation::Add)
            throw std::runtime_error(
                "Primary-region probe initial block is not additive");
        source_by_owner.emplace(operations[index].owner_id,index);
    }
    std::vector<std::vector<std::size_t>> treatments(source_count);
    std::size_t treatment_last=source_count-1;
    for(std::size_t index=source_count;index<operations.size();++index) {
        const auto* request=std::get_if<kernel::FilletRequest>(
            &operations[index].primitive);
        if(!request||request->mode!=kernel::FilletRequest::Mode::Constant)
            break;
        std::optional<std::size_t> source_index;
        bool local=!request->edges.empty();
        for(const auto& reference:request->edges) {
            const auto found=source_by_owner.find(reference.owner_id);
            if(found==source_by_owner.end()||
               (source_index&&*source_index!=found->second)) {
                local=false;break;
            }
            source_index=found->second;
        }
        if(!local||!source_index)break;
        treatments[*source_index].push_back(index);
        treatment_last=index;
    }
    const auto treatment_count=std::accumulate(treatments.begin(),
        treatments.end(),std::size_t{},[](std::size_t result,
                const auto& values) {return result+values.size();});
    if(treatment_count==0)
        throw std::runtime_error(
            "Primary-region probe found no source-local Fillet");
    double tolerance=1.0e-7;
    for(std::size_t index=0;index<=treatment_last;++index)
        tolerance=std::max(tolerance,operations[index].boolean_tolerance);

    struct RegionResult {
        TopoDS_Shape shape;
        double total_ms{},region_wall_ms{},propagation_ms{},fuse_ms{},
            unify_ms{},graph_ms{},split_ms{},classify_ms{},sew_ms{},
            identity_ms{};
        std::size_t identity_count{};
        std::vector<double> region_ms;
    };
    // removal_mode: 0 = fuse treated regions, 1 = trim every region by the
    // other regions' domains, 2 = fuse base regions and materialize every
    // primary removal domain once on the combined boundary, 3 = compose the
    // positive bases and negative treatment domains directly from face patches.
    const auto calculate_regions=[&](bool custom,int removal_mode) {
        EnvironmentVariableScope environment(
            "ZIMA_CPP_EXPERIMENTAL_PLANAR_FILLET",custom?"1":nullptr);
        const auto total_start=Clock::now();
        struct CalculatedRegion {
            TopoDS_Shape base,treated;
            std::vector<TopoDS_Shape> removal_domains;
            double elapsed_ms{};
        };
        std::vector<std::future<CalculatedRegion>> pending;
        pending.reserve(source_count);
        for(std::size_t source_index=0;source_index<source_count;
                ++source_index)
            pending.push_back(std::async(std::launch::async,
                [&,source_index] {
                    std::vector<kernel::HistoryOperation> local{
                        operations[source_index]};
                    for(const auto treatment:treatments[source_index])
                        local.push_back(operations[treatment]);
                    kernel::OcctKernel geometry;
                    const auto start=Clock::now();
                    auto base_calculated=geometry.evaluate_history(
                        {operations[source_index]});
                    if(base_calculated.size()!=1||
                       !base_calculated.back().calculation_errors.empty())
                        throw std::runtime_error(
                            "Primary-region base calculation failed");
                    auto base=kernel::read_calculated_body_shape(
                        base_calculated.back());
                    TopoDS_Shape shape;
                    std::vector<TopoDS_Shape> removal_domains;
                    if(custom&&removal_mode!=0) {
                        if(treatments[source_index].size()!=1)
                            throw std::runtime_error(
                                "Primary removal-domain probe requires one local Fillet per region");
                        const auto& request=std::get<kernel::FilletRequest>(
                            operations[treatments[source_index].front()].primitive);
                        const kernel::BodyResult* native=
                            &base_calculated.back();
                        if(native->kernel_shape==kernel::kernel_body_compound) {
                            const auto output=native->body_outputs.find(
                                operations[source_index].body.id);
                            if(output==native->body_outputs.end())
                                throw std::runtime_error(
                                    "Primary region has no native Body output");
                            native=&output->second.get();
                        }
                        if(native->kernel_shape.empty()||
                           native->kernel_shape==kernel::kernel_body_compound)
                            throw std::runtime_error(
                                "Primary region has no native shape archive");
                        BRep_Builder builder;
                        BRepTools_ShapeSet archive(builder);
                        std::istringstream stream(native->kernel_shape.get());
                        archive.Read(stream);
                        TopoDS_Shape root;
                        archive.Read(root,stream);
                        TopTools_IndexedMapOfShape member_edges;
                        TopExp::MapShapes(root,TopAbs_EDGE,member_edges);
                        std::vector<TopoDS_Edge> selected;
                        for(const auto& reference:request.edges) {
                            bool found=false;
                            for(const auto& binding:native->kernel_edges) {
                                if(binding.reference!=reference)continue;
                                TopoDS_Shape edge;
                                std::istringstream locator(binding.locator);
                                archive.Read(edge,locator);
                                if(edge.ShapeType()!=TopAbs_EDGE||
                                   !member_edges.Contains(edge))continue;
                                found=true;
                                if(std::ranges::none_of(selected,
                                        [&](const auto& current) {
                                            return current.IsSame(edge);
                                        }))selected.push_back(
                                            TopoDS::Edge(edge));
                            }
                            if(!found)throw std::runtime_error(
                                "Primary Fillet edge is absent from its source region");
                        }
                        auto treated=kernel::occt_planar_edge_fillet(
                            root,selected,request.radius_start,
                            std::max(1.0e-7,operations[
                                treatments[source_index].front()].
                                    boolean_tolerance));
                        shape=std::move(treated.shape);
                        removal_domains=std::move(
                            treated.removal_domains);
                    } else {
                        auto calculated=geometry.evaluate_history(local);
                        if(calculated.size()!=local.size()||
                           calculated.empty()||
                           !calculated.back().calculation_errors.empty())
                            throw std::runtime_error(
                                "Primary-region calculation failed");
                        shape=kernel::read_calculated_body_shape(
                            calculated.back());
                    }
                    const double elapsed=
                        std::chrono::duration<double,std::milli>(
                            Clock::now()-start).count();
                    if(base.IsNull()||shape.IsNull()||
                       !BRepCheck_Analyzer(base).IsValid()||
                       !BRepCheck_Analyzer(shape).IsValid())
                        throw std::runtime_error(
                            "Primary-region calculation returned an invalid shape");
                    return CalculatedRegion{std::move(base),
                        std::move(shape),std::move(removal_domains),elapsed};
                }));
        std::vector<TopoDS_Shape> bases,shapes;
        std::vector<std::vector<TopoDS_Shape>> removal_domains;
        RegionResult result;
        bases.reserve(source_count);shapes.reserve(source_count);
        removal_domains.reserve(source_count);
        result.region_ms.reserve(source_count);
        for(auto& future:pending) {
            auto region=future.get();
            bases.push_back(std::move(region.base));
            shapes.push_back(std::move(region.treated));
            removal_domains.push_back(std::move(region.removal_domains));
            result.region_ms.push_back(region.elapsed_ms);
        }
        const auto regions_done=Clock::now();
        result.region_wall_ms=std::chrono::duration<double,std::milli>(
            regions_done-total_start).count();
        if(removal_mode==3) {
            // Enumeration-derived references are calculation-only labels for
            // this benchmark. They are never persisted or returned as native
            // document topology identities.
            const auto boundary_input=[](const TopoDS_Shape& shape,
                    const std::string& owner,std::string_view role,
                    std::size_t history_index) {
                kernel::OcctBoundarySolid boundary;
                boundary.shape=shape;
                TopTools_IndexedMapOfShape faces;
                TopExp::MapShapes(shape,TopAbs_FACE,faces);
                boundary.faces.reserve(
                    static_cast<std::size_t>(faces.Extent()));
                for(int face=1;face<=faces.Extent();++face)
                    boundary.faces.push_back({TopoDS::Face(faces(face)),
                        {{owner,std::string(role)+":face:"+
                            std::to_string(face-1),{}}},history_index});
                return boundary;
            };
            std::vector<kernel::OcctBoundarySolid> positive_inputs;
            positive_inputs.reserve(bases.size());
            for(std::size_t source_index=0;source_index<bases.size();
                    ++source_index)
                positive_inputs.push_back(boundary_input(bases[source_index],
                    operations[source_index].owner_id,"source",
                    source_index));
            const auto positive_started=Clock::now();
            auto positive=kernel::occt_boundary_union(
                positive_inputs,tolerance);
            result.fuse_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-positive_started).count();
            kernel::OcctBoundarySolid positive_boundary;
            positive_boundary.shape=positive.shape;
            positive_boundary.faces.reserve(positive.faces.size());
            for(auto& face:positive.faces)
                positive_boundary.faces.push_back({face.shape,
                    std::move(face.references),face.history_index});
            std::vector<kernel::OcctBoundaryRegion> regions;
            regions.push_back({std::move(positive_boundary),true});
            std::size_t domain_index{};
            for(std::size_t source_index=0;
                    source_index<removal_domains.size();++source_index)
                for(const auto& domain:removal_domains[source_index]) {
                    const auto treatment=treatments[source_index].front();
                    regions.push_back({boundary_input(domain,
                        operations[treatment].owner_id,
                        "removal-domain:"+std::to_string(domain_index++),
                        treatment),false});
                }
            if(domain_index==0)throw std::runtime_error(
                "Primary-region signed composition has no removal domains");
            const auto composition_started=Clock::now();
            auto boundary=kernel::occt_boundary_composition(
                regions,tolerance);
            result.propagation_ms=
                std::chrono::duration<double,std::milli>(
                    Clock::now()-composition_started).count();
            result.graph_ms=boundary.metrics.graph_ms;
            result.split_ms=boundary.metrics.split_ms;
            result.classify_ms=boundary.metrics.classify_ms;
            result.sew_ms=boundary.metrics.sew_ms;
            result.unify_ms=boundary.metrics.unify_ms;
            std::vector<std::string> history_owners;
            history_owners.reserve(operations.size());
            for(const auto& operation:operations)
                history_owners.push_back(operation.owner_id);
            const auto identity_started=Clock::now();
            const auto named=
                kernel::occt_boundary_composition_named_topology(
                    boundary,history_owners,tolerance);
            result.identity_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-identity_started).count();
            result.identity_count=named.faces.size()+named.edges.size()+
                named.vertices.size();
            if(named.faces.size()!=boundary.faces.size()||
               named.edges.size()!=boundary.edges.size()||
               named.vertices.size()!=boundary.vertices.size())
                throw std::runtime_error(
                    "Primary-region signed topology naming is incomplete");
            result.shape=std::move(boundary.shape);
            result.total_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-total_start).count();
            if(result.shape.IsNull()||
               !BRepCheck_Analyzer(result.shape).IsValid())
                throw std::runtime_error(
                    "Primary-region signed composition is invalid");
            return result;
        }
        if(removal_mode==1&&shapes.size()>1) {
            std::vector<std::vector<TopoDS_Shape>> removals(shapes.size());
            for(std::size_t source_index=0;source_index<shapes.size();
                    ++source_index) {
                if(treatments[source_index].empty())continue;
                if(!removal_domains[source_index].empty()) {
                    removals[source_index]=std::move(
                        removal_domains[source_index]);
                    continue;
                }
                BRepAlgoAPI_Cut removal(
                    bases[source_index],shapes[source_index]);
                removal.SetRunParallel(true);
                removal.SetFuzzyValue(tolerance);removal.Build();
                if(!removal.IsDone()||removal.Shape().IsNull())
                    throw std::runtime_error(
                        "Primary-region removal extraction failed");
                GProp_GProps properties;
                BRepGProp::VolumeProperties(removal.Shape(),properties);
                if(std::abs(properties.Mass())>1.0e-9)
                    removals[source_index].push_back(removal.Shape());
            }
            for(std::size_t target=0;target<shapes.size();++target) {
                NCollection_List<TopoDS_Shape> arguments,tools;
                arguments.Append(shapes[target]);
                for(std::size_t source_index=0;
                        source_index<removals.size();++source_index)
                    if(source_index!=target)
                        for(const auto& removal:removals[source_index])
                            tools.Append(removal);
                if(tools.IsEmpty())continue;
                BRepAlgoAPI_Cut trim;
                trim.SetArguments(arguments);trim.SetTools(tools);
                trim.SetRunParallel(true);trim.SetFuzzyValue(tolerance);
                trim.Build();
                if(!trim.IsDone()||trim.Shape().IsNull()||
                   !BRepCheck_Analyzer(trim.Shape()).IsValid())
                    throw std::runtime_error(
                        "Primary-region removal propagation failed");
                shapes[target]=trim.Shape();
            }
        }
        const auto propagation_done=Clock::now();
        result.propagation_ms=std::chrono::duration<double,std::milli>(
            propagation_done-regions_done).count();
        if(removal_mode==2)shapes=bases;
        if(shapes.size()==1)result.shape=shapes.front();
        else {
            TopTools_ListOfShape arguments,tools;
            arguments.Append(shapes.front());
            for(std::size_t index=1;index<shapes.size();++index)
                tools.Append(shapes[index]);
            BRepAlgoAPI_BooleanOperation fuse;
            fuse.SetOperation(BOPAlgo_FUSE);fuse.SetArguments(arguments);
            fuse.SetTools(tools);fuse.SetRunParallel(true);
            fuse.SetFuzzyValue(tolerance);fuse.Build();
            const auto fuse_done=Clock::now();
            result.fuse_ms=std::chrono::duration<double,std::milli>(
                fuse_done-propagation_done).count();
            if(!fuse.IsDone()||fuse.Shape().IsNull())
                throw std::runtime_error(
                    "Primary-region final union failed");
            TopoDS_Shape material=fuse.Shape();
            if(removal_mode==2) {
                NCollection_List<TopoDS_Shape> cut_arguments,cut_tools;
                cut_arguments.Append(material);
                for(const auto& domains:removal_domains)
                    for(const auto& domain:domains)cut_tools.Append(domain);
                if(cut_tools.IsEmpty())throw std::runtime_error(
                    "Primary-region global materialization has no removal domains");
                const auto removal_started=Clock::now();
                BRepAlgoAPI_Cut removal;
                removal.SetArguments(cut_arguments);
                removal.SetTools(cut_tools);removal.SetRunParallel(false);
                removal.SetFuzzyValue(tolerance);removal.Build();
                result.propagation_ms=
                    std::chrono::duration<double,std::milli>(
                        Clock::now()-removal_started).count();
                if(!removal.IsDone()||removal.Shape().IsNull())
                    throw std::runtime_error(
                        "Primary-region global removal materialization failed");
                material=removal.Shape();
            }
            const auto unify_started=Clock::now();
            ShapeUpgrade_UnifySameDomain unify(
                material,true,true,false);
            unify.SetLinearTolerance(tolerance);unify.Build();
            result.shape=unify.Shape();
            result.unify_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-unify_started).count();
        }
        result.total_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-total_start).count();
        if(result.shape.IsNull()||
           !BRepCheck_Analyzer(result.shape).IsValid())
            throw std::runtime_error(
                "Primary-region final union is invalid");
        return result;
    };

    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    kernel::OcctKernel reference_kernel;
    const std::vector<kernel::HistoryOperation> reference_operations(
        operations.begin(),operations.begin()+
            static_cast<std::ptrdiff_t>(treatment_last+1));
    const auto reference_start=Clock::now();
    auto reference_results=reference_kernel.evaluate_history(
        reference_operations);
    const double reference_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-reference_start).count();
    if(reference_results.empty()||
       !reference_results.back().calculation_errors.empty())
        throw std::runtime_error(
            "Primary-region reference calculation failed");
    const auto reference_shape=kernel::read_calculated_body_shape(
        reference_results.back());
    auto primary_occt=calculate_regions(false,0);
    auto primary_custom=calculate_regions(true,0);
    auto propagated_occt=calculate_regions(false,1);
    auto propagated_custom=calculate_regions(true,1);
    auto global_custom=calculate_regions(true,2);
    auto signed_custom=calculate_regions(true,3);
    const auto difference=[&](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left,right);
        cut.SetRunParallel(false);cut.SetFuzzyValue(tolerance);cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto counts=[](const TopoDS_Shape& shape) {
        std::array<std::size_t,3> result{};
        for(const auto [slot,kind]:std::array{
                std::pair{0U,TopAbs_FACE},std::pair{1U,TopAbs_EDGE},
                std::pair{2U,TopAbs_VERTEX}}) {
            TopTools_IndexedMapOfShape values;
            TopExp::MapShapes(shape,kind,values);
            result[slot]=static_cast<std::size_t>(values.Extent());
        }
        return result;
    };
    const auto reference_counts=counts(reference_shape);
    const auto occt_counts=counts(primary_occt.shape);
    const auto custom_counts=counts(primary_custom.shape);
    const auto propagated_occt_counts=counts(propagated_occt.shape);
    const auto propagated_custom_counts=counts(propagated_custom.shape);
    const auto global_custom_counts=counts(global_custom.shape);
    const auto signed_custom_counts=counts(signed_custom.shape);
    const double reference_minus_occt=difference(
        reference_shape,primary_occt.shape);
    const double occt_minus_reference=difference(
        primary_occt.shape,reference_shape);
    const double reference_minus_custom=difference(
        reference_shape,primary_custom.shape);
    const double custom_minus_reference=difference(
        primary_custom.shape,reference_shape);
    const double reference_minus_propagated_occt=difference(
        reference_shape,propagated_occt.shape);
    const double propagated_occt_minus_reference=difference(
        propagated_occt.shape,reference_shape);
    const double reference_minus_propagated_custom=difference(
        reference_shape,propagated_custom.shape);
    const double propagated_custom_minus_reference=difference(
        propagated_custom.shape,reference_shape);
    const double reference_minus_global_custom=difference(
        reference_shape,global_custom.shape);
    const double global_custom_minus_reference=difference(
        global_custom.shape,reference_shape);
    const double reference_minus_signed_custom=difference(
        reference_shape,signed_custom.shape);
    const double signed_custom_minus_reference=difference(
        signed_custom.shape,reference_shape);
    const auto print_region_times=[](const auto& values) {
        for(std::size_t index=0;index<values.size();++index) {
            if(index)std::cout<<',';
            std::cout<<values[index];
        }
    };
    std::cout<<std::setprecision(17)
        <<"primary_region_treatment regions="<<source_count
        <<" treatments="<<treatment_count
        <<" treatment_last="<<treatment_last
        <<" reference_ms="<<reference_ms
        <<" primary_occt_ms="<<primary_occt.total_ms
        <<" primary_occt_region_wall_ms="<<primary_occt.region_wall_ms
        <<" primary_occt_fuse_ms="<<primary_occt.fuse_ms
        <<" primary_occt_unify_ms="<<primary_occt.unify_ms
        <<" primary_occt_region_ms=";
    print_region_times(primary_occt.region_ms);
    std::cout<<" primary_custom_ms="<<primary_custom.total_ms
        <<" primary_custom_region_wall_ms="<<primary_custom.region_wall_ms
        <<" primary_custom_fuse_ms="<<primary_custom.fuse_ms
        <<" primary_custom_unify_ms="<<primary_custom.unify_ms
        <<" primary_custom_region_ms=";
    print_region_times(primary_custom.region_ms);
    std::cout<<" propagated_occt_ms="<<propagated_occt.total_ms
        <<" propagated_occt_region_wall_ms="
        <<propagated_occt.region_wall_ms
        <<" propagated_occt_removal_ms="<<propagated_occt.propagation_ms
        <<" propagated_occt_fuse_ms="<<propagated_occt.fuse_ms
        <<" propagated_occt_unify_ms="<<propagated_occt.unify_ms
        <<" propagated_custom_ms="<<propagated_custom.total_ms
        <<" propagated_custom_region_wall_ms="
        <<propagated_custom.region_wall_ms
        <<" propagated_custom_removal_ms="
        <<propagated_custom.propagation_ms
        <<" propagated_custom_fuse_ms="<<propagated_custom.fuse_ms
        <<" propagated_custom_unify_ms="<<propagated_custom.unify_ms
        <<" global_custom_ms="<<global_custom.total_ms
        <<" global_custom_region_wall_ms="<<global_custom.region_wall_ms
        <<" global_custom_removal_ms="<<global_custom.propagation_ms
        <<" global_custom_fuse_ms="<<global_custom.fuse_ms
        <<" global_custom_unify_ms="<<global_custom.unify_ms
        <<" signed_custom_ms="<<signed_custom.total_ms
        <<" signed_custom_region_wall_ms="<<signed_custom.region_wall_ms
        <<" signed_custom_positive_union_ms="<<signed_custom.fuse_ms
        <<" signed_custom_composition_ms="<<signed_custom.propagation_ms
        <<" signed_custom_graph_ms="<<signed_custom.graph_ms
        <<" signed_custom_split_ms="<<signed_custom.split_ms
        <<" signed_custom_classify_ms="<<signed_custom.classify_ms
        <<" signed_custom_sew_ms="<<signed_custom.sew_ms
        <<" signed_custom_unify_ms="<<signed_custom.unify_ms
        <<" signed_custom_identity_ms="<<signed_custom.identity_ms
        <<" signed_custom_identities="<<signed_custom.identity_count;
    std::cout<<" reference_minus_primary_occt="<<reference_minus_occt
        <<" primary_occt_minus_reference="<<occt_minus_reference
        <<" reference_minus_primary_custom="<<reference_minus_custom
        <<" primary_custom_minus_reference="<<custom_minus_reference
        <<" reference_minus_propagated_occt="
        <<reference_minus_propagated_occt
        <<" propagated_occt_minus_reference="
        <<propagated_occt_minus_reference
        <<" reference_minus_propagated_custom="
        <<reference_minus_propagated_custom
        <<" propagated_custom_minus_reference="
        <<propagated_custom_minus_reference
        <<" reference_minus_global_custom="
        <<reference_minus_global_custom
        <<" global_custom_minus_reference="
        <<global_custom_minus_reference
        <<" reference_minus_signed_custom="
        <<reference_minus_signed_custom
        <<" signed_custom_minus_reference="
        <<signed_custom_minus_reference
        <<" reference_faces="<<reference_counts[0]
        <<" primary_occt_faces="<<occt_counts[0]
        <<" primary_custom_faces="<<custom_counts[0]
        <<" propagated_occt_faces="<<propagated_occt_counts[0]
        <<" propagated_custom_faces="<<propagated_custom_counts[0]
        <<" global_custom_faces="<<global_custom_counts[0]
        <<" signed_custom_faces="<<signed_custom_counts[0]
        <<" reference_edges="<<reference_counts[1]
        <<" primary_occt_edges="<<occt_counts[1]
        <<" primary_custom_edges="<<custom_counts[1]
        <<" propagated_occt_edges="<<propagated_occt_counts[1]
        <<" propagated_custom_edges="<<propagated_custom_counts[1]
        <<" global_custom_edges="<<global_custom_counts[1]
        <<" signed_custom_edges="<<signed_custom_counts[1]
        <<" reference_vertices="<<reference_counts[2]
        <<" primary_occt_vertices="<<occt_counts[2]
        <<" primary_custom_vertices="<<custom_counts[2]
        <<" propagated_occt_vertices="<<propagated_occt_counts[2]
        <<" propagated_custom_vertices="<<propagated_custom_counts[2]
        <<" global_custom_vertices="<<global_custom_counts[2]
        <<" signed_custom_vertices="<<signed_custom_counts[2]
        <<'\n';
    const auto equivalent=[](double first,double second) {
        return first>=0&&second>=0&&first<=1.0e-6&&second<=1.0e-6;
    };
    return equivalent(reference_minus_global_custom,
               global_custom_minus_reference)&&
           equivalent(reference_minus_signed_custom,
               signed_custom_minus_reference)&&
           signed_custom_counts==reference_counts?0:2;
}

int direct_exact_equivalence_probe(
        const std::filesystem::path& source,std::size_t requested_count=0) {
    using namespace zima;
    std::vector<kernel::BodyResult> persisted;
    const auto document=document::PartDocument::load(source,&persisted);
    auto authored=document.kernel_operations(false,true);
    if(requested_count) {
        if(requested_count<2||requested_count>authored.size())
            throw std::runtime_error(
                "Direct exact prefix is outside Part history");
        authored.resize(requested_count);
    }
    if(authored.size()<2)
        throw std::runtime_error(
            "Direct exact probe requires a modeling history");
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);

    kernel::OcctKernel serial_kernel;
    const auto serial_start=Clock::now();
    const auto serial=serial_kernel.evaluate_history(authored);
    const auto serial_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-serial_start).count();
    if(serial.size()!=authored.size()||
       !serial.back().calculation_errors.empty())
        throw std::runtime_error(
            "Serial reference history failed");

    const auto local_body=[&](const kernel::BodyResult& boundary,
            std::size_t operation_index)->const kernel::BodyResult& {
        const auto& body_id=authored.at(operation_index).body.id;
        const auto output=boundary.body_outputs.find(body_id);
        return output==boundary.body_outputs.end()
            ?boundary:output->second.get();
    };
    const auto direct_prefix=[&](
            const std::vector<kernel::HistoryOperation>& operations,
            std::size_t count) {
        std::vector<kernel::HistoryOperation> prefix(
            operations.begin(),operations.begin()+
                static_cast<std::ptrdiff_t>(count));
        kernel::HistoryOutputPolicy policy;
        policy.final_boundary_first=true;
        kernel::OcctKernel geometry;
        auto result=geometry.evaluate_history_recovering(
            prefix,{},policy);
        if(result.size()!=count||
           !result.back().calculation_errors.empty())
            throw std::runtime_error(
                "Direct exact prefix failed");
        return result;
    };
    const auto& direct_operations=authored;
    const auto edge_treatments=std::ranges::count_if(
        direct_operations,[](const auto& operation) {
            return std::holds_alternative<kernel::FilletRequest>(
                       operation.primitive)||
                std::holds_alternative<kernel::ChamferRequest>(
                       operation.primitive);
        });

    const auto direct_start=Clock::now();
    const auto direct=direct_prefix(
        direct_operations,direct_operations.size());
    const auto direct_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-direct_start).count();
    const auto& serial_final=local_body(
        serial.back(),direct_operations.size()-1);
    const auto& direct_final=local_body(
        direct.back(),direct_operations.size()-1);
    const auto serial_shape=kernel::read_calculated_body_shape(
        serial_final);
    const auto direct_shape=kernel::read_calculated_body_shape(
        direct_final);
    if(std::getenv("ZIMA_CPP_DIRECT_QUICK_EQUIVALENCE")) {
        struct QuickMetrics {
            double volume{},area{};
            gp_Pnt center;
            std::array<int,4> topology{};
            std::set<std::string> references;
            bool valid{};
        };
        const auto metrics=[](const TopoDS_Shape& shape,
                const kernel::BodyResult& body) {
            QuickMetrics value;
            GProp_GProps volume,surface;
            BRepGProp::VolumeProperties(shape,volume);
            BRepGProp::SurfaceProperties(shape,surface);
            value.volume=volume.Mass();
            value.area=surface.Mass();
            value.center=volume.CentreOfMass();
            for(std::size_t index=0;index<value.topology.size();++index) {
                TopTools_IndexedMapOfShape shapes;
                TopExp::MapShapes(shape,std::array{TopAbs_SOLID,
                    TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}[index],shapes);
                value.topology[index]=shapes.Extent();
            }
            const auto append=[&](char kind,const auto& bindings) {
                for(const auto& binding:bindings)
                    value.references.insert(std::string(1,kind)+'\x1f'+
                        binding.reference.owner_id+'\x1f'+
                        binding.reference.semantic_key+'\x1f'+
                        binding.reference.instance_path);
            };
            append('F',body.kernel_faces);
            append('E',body.kernel_edges);
            append('V',body.kernel_vertices);
            value.valid=BRepCheck_Analyzer(
                shape,true,true,false).IsValid();
            return value;
        };
        const auto material_equivalent=[](const QuickMetrics& first,
                const QuickMetrics& second) {
            const auto close=[](double left,double right,
                    double absolute,double relative) {
                return std::abs(left-right)<=std::max(absolute,
                    relative*std::max(std::abs(left),std::abs(right)));
            };
            return first.valid&&second.valid&&
                close(first.volume,second.volume,1.0e-6,1.0e-9)&&
                close(first.area,second.area,1.0e-6,1.0e-9)&&
                first.center.Distance(second.center)<=1.0e-7&&
                first.topology==second.topology;
        };
        const auto exactly_equivalent=[&](const QuickMetrics& first,
                const QuickMetrics& second) {
            return material_equivalent(first,second)&&
                first.references==second.references;
        };
        const auto serial_metrics=metrics(serial_shape,serial_final);
        const auto direct_metrics=metrics(direct_shape,direct_final);
        double repeated_direct_ms=-1.;
        bool repeated_direct_equal=true;
        if(std::getenv("ZIMA_CPP_DIRECT_REPEAT")) {
            const auto repeated_start=Clock::now();
            const auto repeated=direct_prefix(
                direct_operations,direct_operations.size());
            repeated_direct_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-repeated_start).count();
            const auto& repeated_final=local_body(
                repeated.back(),direct_operations.size()-1);
            repeated_direct_equal=exactly_equivalent(direct_metrics,metrics(
                kernel::read_calculated_body_shape(repeated_final),
                repeated_final));
        }
        std::vector<std::string> missing_references,added_references;
        std::set_difference(serial_metrics.references.begin(),
            serial_metrics.references.end(),
            direct_metrics.references.begin(),
            direct_metrics.references.end(),
            std::back_inserter(missing_references));
        std::set_difference(direct_metrics.references.begin(),
            direct_metrics.references.end(),
            serial_metrics.references.begin(),
            serial_metrics.references.end(),
            std::back_inserter(added_references));
        const auto categories=[](const auto& references) {
            std::map<std::string,std::size_t> counts;
            for(const auto& encoded:references) {
                const auto kind_end=encoded.find('\x1f');
                const auto owner_end=kind_end==std::string::npos
                    ?std::string::npos:encoded.find('\x1f',kind_end+1);
                const auto key_end=owner_end==std::string::npos
                    ?std::string::npos:encoded.find('\x1f',owner_end+1);
                const auto key=owner_end==std::string::npos
                    ?std::string_view{}:std::string_view(encoded).substr(
                        owner_end+1,key_end-(owner_end+1));
                std::string category=kind_end==std::string::npos
                    ?"?":encoded.substr(0,kind_end);
                if(key.starts_with("boundary-union:add:"))
                    category+=":boundary-union:add";
                else if(key.starts_with(
                        "boolean:add:intersection:between:"))
                    category+=":boolean:add:intersection";
                else if(key.starts_with("boolean:add:split-edge:from:"))
                    category+=":boolean:add:split-edge";
                else if(key.starts_with("boolean:add:vertex"))
                    category+=":boolean:add:vertex";
                else if(key.starts_with("boolean:add:"))
                    category+=":boolean:add:other";
                else if(key.starts_with("fillet:"))
                    category+=":fillet";
                else if(key.starts_with("chamfer:"))
                    category+=":chamfer";
                else category+=":source";
                ++counts[category];
            }
            std::string result;
            for(const auto& [category,count]:counts) {
                if(!result.empty())result+=',';
                result+=category+'='+std::to_string(count);
            }
            return result;
        };
        bool persisted_available{};
        bool persisted_equal{};
        if(persisted.size()>=direct_operations.size()) {
            const auto& persisted_body=local_body(
                persisted[direct_operations.size()-1],
                direct_operations.size()-1);
            if(!persisted_body.kernel_shape.empty()) {
                const auto persisted_shape=
                    kernel::read_calculated_body_shape(persisted_body);
                persisted_available=!persisted_shape.IsNull();
                if(persisted_available)
                    persisted_equal=exactly_equivalent(direct_metrics,
                        metrics(persisted_shape,persisted_body));
            }
        }
        const bool direct_material_equal=material_equivalent(
            serial_metrics,direct_metrics);
        const bool direct_identity_equal=
            serial_metrics.references==direct_metrics.references;
        std::cout<<std::setprecision(17)
            <<"direct_quick_equivalence operations="
                <<direct_operations.size()
            <<" serial_ms="<<serial_ms
            <<" direct_ms="<<direct_ms
            <<" repeated_direct_ms="<<repeated_direct_ms
            <<" repeated_direct_equal="<<repeated_direct_equal
            <<" serial_volume="<<serial_metrics.volume
            <<" direct_volume="<<direct_metrics.volume
            <<" serial_area="<<serial_metrics.area
            <<" direct_area="<<direct_metrics.area
            <<" center_distance="<<serial_metrics.center.Distance(
                direct_metrics.center)
            <<" serial_topology="<<serial_metrics.topology[0]<<','
                <<serial_metrics.topology[1]<<','
                <<serial_metrics.topology[2]<<','
                <<serial_metrics.topology[3]
            <<" direct_topology="<<direct_metrics.topology[0]<<','
                <<direct_metrics.topology[1]<<','
                <<direct_metrics.topology[2]<<','
                <<direct_metrics.topology[3]
            <<" serial_references="<<serial_metrics.references.size()
            <<" direct_references="<<direct_metrics.references.size()
            <<" missing_references="<<missing_references.size()
            <<" added_references="<<added_references.size()
            <<" missing_categories="<<categories(missing_references)
            <<" added_categories="<<categories(added_references)
            <<" direct_material_equal="<<direct_material_equal
            <<" direct_identity_equal="<<direct_identity_equal
            <<" persisted_available="<<persisted_available
            <<" persisted_equal="<<persisted_equal<<'\n';
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND")) {
            for(const auto& reference:std::span(missing_references).
                    first(std::min<std::size_t>(8,
                        missing_references.size())))
                std::cerr<<"direct quick missing reference "
                    <<reference<<'\n';
            for(const auto& reference:std::span(added_references).
                    first(std::min<std::size_t>(8,
                        added_references.size())))
                std::cerr<<"direct quick added reference "
                    <<reference<<'\n';
        }
        if(!direct_material_equal||
                (persisted_available&&!persisted_equal)||
                !repeated_direct_equal)
            throw std::runtime_error(
                "Quick direct history equivalence failed");
        return 0;
    }
    if(const char* output_directory=
            std::getenv("ZIMA_CPP_DIRECT_VISUALIZATION_DIRECTORY")) {
        const auto directory=std::filesystem::u8path(output_directory);
        std::filesystem::create_directories(directory);
        const auto write_wire=[&](const kernel::BodyResult& boundary,
                const std::filesystem::path& target) {
            nlohmann::json packet;
            packet["operation_count"]=direct_operations.size();
            auto& edges=packet["edges"]=nlohmann::json::array();
            std::size_t edge_index{};
            for(const auto& edge:boundary.mesh.edges) {
                if(edge.parameter_seam||edge.points.size()<2)continue;
                nlohmann::json item;
                item["id"]=edge_index++;
                auto& points=item["points"]=nlohmann::json::array();
                constexpr std::size_t maximum_points=16;
                const auto count=std::min(maximum_points,
                    edge.points.size());
                const auto coordinate=[](double value) {
                    return std::round(value*1000.)/1000.;
                };
                for(std::size_t sample=0;sample<count;++sample) {
                    const auto index=count==1?std::size_t{}:
                        sample*(edge.points.size()-1)/(count-1);
                    const auto& point=edge.points[index];
                    points.push_back({coordinate(point.x),
                        coordinate(point.y),coordinate(point.z)});
                }
                edges.push_back(std::move(item));
            }
            std::ofstream stream(target,std::ios::binary);
            stream<<packet.dump();
            if(!stream)
                throw std::runtime_error(
                    "Could not write direct topology visualization packet");
        };
        write_wire(serial.back(),directory/
            ("serial-prefix-"+std::to_string(
                direct_operations.size())+"-wire.json"));
        write_wire(direct.back(),directory/
            ("direct-prefix-"+std::to_string(
                direct_operations.size())+"-wire.json"));
    }
    const auto difference=[](const TopoDS_Shape& left,
            const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left,right);
        cut.SetRunParallel(true);
        cut.SetFuzzyValue(1.0e-7);
        cut.Build();
        if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
        GProp_GProps properties;
        BRepGProp::VolumeProperties(cut.Shape(),properties);
        return std::abs(properties.Mass());
    };
    const auto direct_minus_serial=difference(
        direct_shape,serial_shape);
    const auto serial_minus_direct=difference(
        serial_shape,direct_shape);
    double persisted_minus_serial=-1.,serial_minus_persisted=-1.,
        persisted_minus_direct=-1.,direct_minus_persisted=-1.;
    if(persisted.size()>=direct_operations.size()) {
        const auto& persisted_body=local_body(
            persisted[direct_operations.size()-1],
            direct_operations.size()-1);
        // Final-first native documents intentionally leave ordinary
        // intermediate rows without a persisted B-Rep. Compare a saved
        // boundary only when the requested prefix is one of the explicitly
        // retained complete outputs.
        if(!persisted_body.kernel_shape.empty()) {
            const auto persisted_shape=kernel::read_calculated_body_shape(
                persisted_body);
            persisted_minus_serial=difference(
                persisted_shape,serial_shape);
            serial_minus_persisted=difference(
                serial_shape,persisted_shape);
            persisted_minus_direct=difference(
                persisted_shape,direct_shape);
            direct_minus_persisted=difference(
                direct_shape,persisted_shape);
        }
    }
    std::cout<<std::setprecision(17)
        <<"direct_exact_equivalence operations="
            <<direct_operations.size()
        <<" edge_treatments="<<edge_treatments
        <<" serial_ms="<<serial_ms
        <<" direct_ms="<<direct_ms
        <<" direct_minus_serial="<<direct_minus_serial
        <<" serial_minus_direct="<<serial_minus_direct
        <<" persisted_minus_serial="<<persisted_minus_serial
        <<" serial_minus_persisted="<<serial_minus_persisted
        <<" persisted_minus_direct="<<persisted_minus_direct
        <<" direct_minus_persisted="<<direct_minus_persisted<<'\n';
    if(direct_minus_serial<0||serial_minus_direct<0||
       direct_minus_serial>1.0e-6||serial_minus_direct>1.0e-6)
        throw std::runtime_error(
            "Exact direct history changed material");
    return 0;
}

int aggregate_property_demand_probe(std::size_t feature_count) {
    using namespace zima;
    if(feature_count<1||feature_count>128)
        throw std::invalid_argument("Property demand feature count must be 1..128");
    const auto part=part_fixture(feature_count);
    const auto operations=part.kernel_operations();
    const auto calculate=[&](kernel::AggregatePropertyDemand demand) {
        kernel::OcctKernel kernel;
        kernel::AggregatePropertyScope properties(demand);
        return kernel.evaluate_history(operations);
    };
    std::vector<kernel::BodyResult> exact,deferred;
    const auto exact_ms=milliseconds([&]{exact=calculate(
        kernel::AggregatePropertyDemand::All);},3);
    const auto deferred_ms=milliseconds([&]{deferred=calculate(
        kernel::AggregatePropertyDemand::Deferred);},3);
    const auto geometry_packet=[](const kernel::BodyResult& result) {
        auto packet=document::serialize_body_result(result);
        const auto erase_analysis=[&](const auto& self,nlohmann::json& value)->void {
            if(value.is_object()) {
                for(const auto* field:{"volume","surface_area","volume_integrals",
                        "surface_centroid","solid_present","incremental_history"})value.erase(field);
                for(auto& child:value.items())self(self,child.value());
            } else if(value.is_array())for(auto& child:value)self(self,child);
        };
        erase_analysis(erase_analysis,packet);return packet;
    };
    const auto exact_geometry=geometry_packet(exact.back());
    const auto deferred_geometry=geometry_packet(deferred.back());
    if(exact.size()!=operations.size()||deferred.size()!=exact.size()||
            deferred.back().physical_properties_current||
            exact_geometry!=deferred_geometry) {
        if(exact_geometry!=deferred_geometry)
            std::cerr<<nlohmann::json::diff(exact_geometry,deferred_geometry).dump().substr(0,4000)<<'\n';
        throw std::runtime_error("Deferred property demand changed calculated geometry or stable references");
    }
    kernel::OcctKernel analysis_kernel;
    const auto analysis_ms=milliseconds([&] {
        auto analyzed=deferred.back();
        analysis_kernel.ensure_body_properties(analyzed);
        if(!analyzed.physical_properties_current||
                std::abs(analyzed.volume-exact.back().volume)>1e-8||
                std::abs(analyzed.surface_area-exact.back().surface_area)>1e-8||
                analyzed.volume_integrals!=exact.back().volume_integrals||
                analyzed.surface_centroid!=exact.back().surface_centroid)
            std::abort();
    },3);
    std::size_t deferred_archive_bytes{};
    for(const auto& boundary:deferred)deferred_archive_bytes+=boundary.kernel_shape.size();
    std::cout<<std::setprecision(17)
        <<"aggregate_property_demand features="<<feature_count
        <<" exact_ms="<<exact_ms
        <<" deferred_ms="<<deferred_ms
        <<" foreground_speedup="<<exact_ms/deferred_ms
        <<" background_final_ms="<<analysis_ms
        <<" archive_bytes="<<deferred_archive_bytes<<'\n';
    return 0;
}

int edit_thin_profile_probe(const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const std::string& container_id,double thickness) {
    using namespace zima;
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto load_start=Clock::now();
    std::vector<kernel::BodyResult> previous;
    auto document=document::PartDocument::load(source,&previous);
    const auto load_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-load_start).count();
    if(previous.empty())throw std::runtime_error(
        "Thin profile edit probe requires a calculated source Part");
    const auto operations=document.kernel_operations(false,true);
    if(operations.empty())throw std::runtime_error(
        "Thin profile edit probe requires modeling history");
    const auto body_id=operations.back().body.id;
    const auto local_body=[&](const kernel::BodyResult& boundary)
            ->const kernel::BodyResult& {
        const auto bodies=boundary.body_boundaries.find(body_id);
        const auto& local=bodies==boundary.body_boundaries.end()
            ?boundary:bodies->second.back();
        const auto output=local.body_outputs.find(body_id);
        return output==local.body_outputs.end()
            ?local:output->second.get();
    };
    const auto policy=workspace::feature_definition_calculation_policy(
        document,previous,container_id);
    auto* feature=document.find_container(container_id);
    if(!feature)throw std::runtime_error(
        "Thin profile edit probe did not find the requested container");
    double* value{};
    switch(feature->feature_kind) {
    case document::FeatureKind::Feature:
        if(feature->feature.result_type==document::ProfileResultType::Thin)
            value=&feature->feature.thin_thickness;
        break;
    case document::FeatureKind::Extrusion:
        if(feature->extrusion.result_type==document::ProfileResultType::Thin)
            value=&feature->extrusion.thin_thickness;
        break;
    case document::FeatureKind::Revolution:
        if(feature->revolution.result_type==document::ProfileResultType::Thin)
            value=&feature->revolution.thin_thickness;
        break;
    default: break;
    }
    if(!value)throw std::runtime_error(
        "Requested container is not a Thin profile feature");
    const bool verify_direct_recalculation=
        std::getenv("ZIMA_CPP_VERIFY_DIRECT_RECALCULATION")!=nullptr;
    std::vector<kernel::BodyResult> direct_baseline;
    double direct_baseline_ms=-1.;
    if(verify_direct_recalculation) {
        kernel::OcctKernel baseline_kernel;
        kernel::HistoryOutputPolicy output_policy;
        output_policy.final_boundary_first=true;
        const auto baseline_start=Clock::now();
        direct_baseline=baseline_kernel.evaluate_history_recovering(
            document.kernel_operations(false,true),{},output_policy);
        direct_baseline_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-baseline_start).count();
        if(direct_baseline.empty()||
           !local_body(direct_baseline.back()).calculation_errors.empty())
            throw std::runtime_error(
                "Thin profile direct baseline did not calculate");
    }
    const double old_thickness=*value;
    *value=thickness;
    kernel::OcctKernel kernel;
    const auto calculation_start=Clock::now();
    std::vector<kernel::BodyResult> calculated;
    if(verify_direct_recalculation) {
        kernel::HistoryOutputPolicy output_policy;
        output_policy.final_boundary_first=true;
        calculated=kernel.evaluate_history_recovering(
            document.kernel_operations(false,true),{},output_policy);
    } else {
        calculated=workspace::calculate_part_with_resolved_references(
            kernel,document,&previous,policy);
    }
    const auto calculation_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-calculation_start).count();
    if(verify_direct_recalculation)
        std::cerr<<"thin direct phase=calculation ms="
            <<calculation_ms<<'\n';
    if(calculated.empty()||calculated.size()!=document.kernel_operations().size())
        throw std::runtime_error(
            "Thin profile edit did not publish every history boundary");
    const auto& calculated_final=local_body(calculated.back());
    if(!calculated_final.calculation_errors.empty())
        throw std::runtime_error(
            "Thin profile edit left a final calculation error");
    const auto calculated_shape=kernel::read_calculated_body_shape(
        calculated_final);
    if(calculated_shape.IsNull()||!BRepCheck_Analyzer(
            calculated_shape,true,true,false).IsValid())
        throw std::runtime_error(
            "Thin profile edit produced an invalid final Body");
    if(verify_direct_recalculation) {
        static_cast<void>(document::load_body_result(
            document::serialize_body_result(calculated_final)));
        std::cerr<<"thin direct phase=final-round-trip done\n";
    } else {
        for(std::size_t index=0;index<calculated.size();++index)try {
            static_cast<void>(document::load_body_result(
                document::serialize_body_result(calculated[index])));
        } catch(const std::exception& error) {
            throw std::runtime_error("Thin profile edit boundary "+
                std::to_string(index)+" does not round-trip: "+
                error.what());
        }
    }
    const auto save_start=Clock::now();
    document.save(destination,calculated);
    const auto save_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-save_start).count();
    if(verify_direct_recalculation)
        std::cerr<<"thin direct phase=save ms="<<save_ms<<'\n';
    const auto reopen_start=Clock::now();
    std::vector<kernel::BodyResult> reopened_boundaries;
    auto reopened=document::PartDocument::load(
        destination,&reopened_boundaries);
    const auto reopen_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-reopen_start).count();
    if(verify_direct_recalculation)
        std::cerr<<"thin direct phase=reopen ms="<<reopen_ms<<'\n';
    if(reopened.serialized()!=document.serialized()||
            reopened_boundaries.size()!=calculated.size()||
            document::serialize_body_result(reopened_boundaries.back())!=
                document::serialize_body_result(calculated.back()))
        throw std::runtime_error(
            "Thin profile edit changed during save/reopen");
    const auto reference_key=[](const auto& reference) {
        return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+
            reference.instance_path;
    };
    const auto references=[&](const auto& result) {
        std::set<std::string> values;
        const auto append=[&](const auto& range) {
            for(const auto& value:range)
                values.insert(reference_key(value.reference));
        };
        for(const auto& value:result.mesh.triangle_references)
            values.insert(reference_key(value));
        append(result.mesh.edges);append(result.mesh.points);
        append(result.mesh.axes);
        for(const auto& value:result.kernel_faces)
            values.insert(reference_key(value.reference));
        for(const auto& value:result.kernel_edges)
            values.insert(reference_key(value.reference));
        for(const auto& value:result.kernel_vertices)
            values.insert(reference_key(value.reference));
        return values;
    };
    const auto previous_references=references(local_body(previous.back()));
    const auto calculated_references=references(calculated_final);
    bool direct_parameter_reference_sets_equal=true;
    std::size_t direct_parameter_missing_references{};
    std::size_t direct_parameter_added_references{};
    if(verify_direct_recalculation) {
        const auto baseline_references=references(
            local_body(direct_baseline.back()));
        direct_parameter_reference_sets_equal=
            baseline_references==calculated_references;
        std::vector<std::string> missing,added;
        std::set_difference(baseline_references.begin(),
            baseline_references.end(),calculated_references.begin(),
            calculated_references.end(),std::back_inserter(missing));
        std::set_difference(calculated_references.begin(),
            calculated_references.end(),baseline_references.begin(),
            baseline_references.end(),std::back_inserter(added));
        direct_parameter_missing_references=missing.size();
        direct_parameter_added_references=added.size();
    }
    bool repeated_reference_sets_equal=true;
    double repeated_calculation_ms=-1.;
    if(verify_direct_recalculation) {
        kernel::OcctKernel repeated_kernel;
        const auto repeated_start=Clock::now();
        kernel::HistoryOutputPolicy output_policy;
        output_policy.final_boundary_first=true;
        auto repeated=repeated_kernel.evaluate_history_recovering(
            reopened.kernel_operations(false,true),{},output_policy);
        repeated_calculation_ms=
            std::chrono::duration<double,std::milli>(
                Clock::now()-repeated_start).count();
        std::cerr<<"thin direct phase=repeated-calculation ms="
            <<repeated_calculation_ms<<'\n';
        if(!repeated.empty()&&repeated.size()==calculated.size()) {
            const auto& repeated_final=local_body(repeated.back());
            const auto repeated_shape=kernel::read_calculated_body_shape(
                repeated_final);
            repeated_reference_sets_equal=
                repeated_final.calculation_errors.empty()&&
                !repeated_shape.IsNull()&&BRepCheck_Analyzer(
                    repeated_shape,true,true,false).IsValid()&&
                references(repeated_final)==calculated_references;
        } else repeated_reference_sets_equal=false;
    }
    std::cout<<std::setprecision(17)
        <<"edit_thin_profile owner="<<container_id
        <<" old_thickness="<<old_thickness
        <<" new_thickness="<<thickness
        <<" boundaries="<<calculated.size()
        <<" load_ms="<<load_ms
        <<" calculation_ms="<<calculation_ms
        <<" direct_baseline_ms="<<direct_baseline_ms
        <<" save_ms="<<save_ms
        <<" reopen_ms="<<reopen_ms
        <<" repeated_calculation_ms="<<repeated_calculation_ms
        <<" repeated_reference_sets_equal="
        <<repeated_reference_sets_equal
        <<" direct_parameter_reference_sets_equal="
        <<direct_parameter_reference_sets_equal
        <<" direct_parameter_missing_references="
        <<direct_parameter_missing_references
        <<" direct_parameter_added_references="
        <<direct_parameter_added_references
        <<" reference_sets_equal="
        <<(previous_references==calculated_references)
        <<" previous_references="<<previous_references.size()
        <<" calculated_references="<<calculated_references.size()
        <<" final_faces="<<calculated_final.kernel_faces.size()
        <<" final_edges="<<calculated_final.kernel_edges.size()
        <<" final_vertices="<<calculated_final.kernel_vertices.size()
        <<'\n';
    if((!verify_direct_recalculation&&
            previous_references!=calculated_references)||
       !repeated_reference_sets_equal||
       !direct_parameter_reference_sets_equal)
        throw std::runtime_error(
            "Thin profile edit changed the stable reference identity set");
    return 0;
}

int main(int argc, char** argv) {
    if(argc==3&&std::string_view(argv[1])=="--operation-summary") {
        try {
            using namespace zima;
            std::vector<kernel::BodyResult> boundaries;
            const auto document=document::PartDocument::load(
                std::filesystem::u8path(argv[2]),&boundaries);
            const auto operations=document.kernel_operations(false,true);
            constexpr std::array<std::string_view,19> primitive_names{
                "Extrusion","Revolution","FeatureGroup","Sweep3D","Step",
                "Fillet","Chamfer","Shell","ThreadSurface","DrillPoint",
                "SheetState","BoundarySurface","SolidState","SurfaceSewing",
                "SurfaceIntersection","SurfaceTrim","SurfaceThicken",
                "SheetForm","Loft"};
            std::cout<<"operation_summary operations="<<operations.size()
                <<" calculated_boundaries="<<boundaries.size()<<'\n';
            for(std::size_t index=0;index<operations.size();++index) {
                const auto& operation=operations[index];
                const auto primitive_index=operation.primitive.index();
                std::cout<<"operation index="<<index
                    <<" primitive_index="<<primitive_index
                    <<" primitive="<<(primitive_index<primitive_names.size()
                        ?primitive_names[primitive_index]:"Unknown")
                    <<" boolean="<<static_cast<int>(operation.operation)
                    <<" suppressed="<<operation.suppressed
                    <<" owner="<<operation.owner_id
                    <<" body="<<operation.body.id
                    <<" cached="<<(index<boundaries.size())<<'\n';
            }
            if(!boundaries.empty()) {
                for(const auto& [body,rows]:boundaries.back().body_boundaries)
                    for(std::size_t index=0;index<rows.size();++index) {
                        const auto& row=rows[index];
                        std::cout<<"body_boundary body="<<body
                            <<" index="<<index
                            <<" vertices="<<row.mesh.vertices.size()
                            <<" triangles="<<row.mesh.triangles.size()/3
                            <<" edges="<<row.mesh.edges.size()
                            <<" faces="<<row.kernel_faces.size()
                            <<" errors="<<row.calculation_errors.size()<<'\n';
                    }
            }
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==5&&std::string_view(argv[1])==
            "--compare-reference-owner") {
        try {
            using namespace zima;
            std::vector<kernel::BodyResult> first_boundaries,
                second_boundaries;
            static_cast<void>(document::PartDocument::load(
                std::filesystem::u8path(argv[2]),&first_boundaries));
            static_cast<void>(document::PartDocument::load(
                std::filesystem::u8path(argv[3]),&second_boundaries));
            if(first_boundaries.empty()||second_boundaries.empty())
                throw std::runtime_error(
                    "Compared Parts require calculated results");
            const std::string owner=argv[4];
            struct Metric {
                std::size_t elements{},samples{};
                double measure{};
                kernel::Vec3 weighted{},minimum{},maximum{};
                bool bounded{};
            };
            struct Signature {
                std::map<std::string,Metric> faces,edges,points;
            };
            const auto key=[](const auto& reference) {
                return reference.owner_id+'\x1f'+reference.semantic_key+
                    '\x1f'+reference.instance_path;
            };
            const auto include=[](Metric& metric,const kernel::Vec3& point) {
                if(!metric.bounded) {
                    metric.minimum=metric.maximum=point;
                    metric.bounded=true;
                } else {
                    metric.minimum={std::min(metric.minimum.x,point.x),
                        std::min(metric.minimum.y,point.y),
                        std::min(metric.minimum.z,point.z)};
                    metric.maximum={std::max(metric.maximum.x,point.x),
                        std::max(metric.maximum.y,point.y),
                        std::max(metric.maximum.z,point.z)};
                }
            };
            const auto signature=[&](const kernel::BodyResult& body) {
                Signature result;
                const auto& geometry=body.mesh.original_references;
                for(std::size_t triangle=0;
                        triangle<geometry.triangle_references.size();
                        ++triangle) {
                    const auto& reference=
                        geometry.triangle_references[triangle];
                    if(reference.owner_id!=owner)continue;
                    auto& metric=result.faces[key(reference)];
                    const auto& a=geometry.vertices.at(
                        geometry.triangles.at(3*triangle));
                    const auto& b=geometry.vertices.at(
                        geometry.triangles.at(3*triangle+1));
                    const auto& c=geometry.vertices.at(
                        geometry.triangles.at(3*triangle+2));
                    const kernel::Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z};
                    const kernel::Vec3 ac{c.x-a.x,c.y-a.y,c.z-a.z};
                    const kernel::Vec3 cross{
                        ab.y*ac.z-ab.z*ac.y,
                        ab.z*ac.x-ab.x*ac.z,
                        ab.x*ac.y-ab.y*ac.x};
                    const double area=.5*std::hypot(
                        cross.x,cross.y,cross.z);
                    ++metric.elements;metric.samples+=3;
                    metric.measure+=area;
                    metric.weighted.x+=area*(a.x+b.x+c.x)/3.;
                    metric.weighted.y+=area*(a.y+b.y+c.y)/3.;
                    metric.weighted.z+=area*(a.z+b.z+c.z)/3.;
                    include(metric,a);include(metric,b);include(metric,c);
                }
                for(const auto& edge:geometry.edges) {
                    if(edge.reference.owner_id!=owner)continue;
                    auto& metric=result.edges[key(edge.reference)];
                    ++metric.elements;metric.samples+=edge.points.size();
                    for(std::size_t point=0;point<edge.points.size();++point) {
                        const auto& value=edge.points[point];
                        include(metric,value);
                        metric.weighted.x+=value.x;
                        metric.weighted.y+=value.y;
                        metric.weighted.z+=value.z;
                        if(point) {
                            const auto& previous=edge.points[point-1];
                            metric.measure+=std::hypot(value.x-previous.x,
                                value.y-previous.y,value.z-previous.z);
                        }
                    }
                }
                for(const auto& point:geometry.points) {
                    if(point.reference.owner_id!=owner)continue;
                    auto& metric=result.points[key(point.reference)];
                    ++metric.elements;++metric.samples;
                    metric.weighted.x+=point.position.x;
                    metric.weighted.y+=point.position.y;
                    metric.weighted.z+=point.position.z;
                    include(metric,point.position);
                }
                return result;
            };
            const auto first=signature(first_boundaries.back());
            const auto second=signature(second_boundaries.back());
            double maximum_difference{};
            const auto compare=[&](const auto& left,const auto& right) {
                if(left.size()!=right.size())return false;
                for(const auto& [reference,a]:left) {
                    const auto found=right.find(reference);
                    if(found==right.end())return false;
                    const auto& b=found->second;
                    if(a.elements!=b.elements||a.samples!=b.samples||
                            a.bounded!=b.bounded)return false;
                    const auto difference=[&](double x,double y) {
                        maximum_difference=std::max(maximum_difference,
                            std::abs(x-y));
                    };
                    difference(a.measure,b.measure);
                    difference(a.weighted.x,b.weighted.x);
                    difference(a.weighted.y,b.weighted.y);
                    difference(a.weighted.z,b.weighted.z);
                    if(a.bounded) {
                        difference(a.minimum.x,b.minimum.x);
                        difference(a.minimum.y,b.minimum.y);
                        difference(a.minimum.z,b.minimum.z);
                        difference(a.maximum.x,b.maximum.x);
                        difference(a.maximum.y,b.maximum.y);
                        difference(a.maximum.z,b.maximum.z);
                    }
                }
                return true;
            };
            const bool structure_equal=compare(first.faces,second.faces)&&
                compare(first.edges,second.edges)&&
                compare(first.points,second.points);
            std::cout<<std::setprecision(17)
                <<"compare_reference_owner owner="<<owner
                <<" structure_equal="<<structure_equal
                <<" maximum_metric_difference="<<maximum_difference
                <<" first_faces="<<first.faces.size()
                <<" second_faces="<<second.faces.size()
                <<" first_edges="<<first.edges.size()
                <<" second_edges="<<second.edges.size()
                <<" first_points="<<first.points.size()
                <<" second_points="<<second.points.size()<<'\n';
            if(!structure_equal||maximum_difference>1.0e-6)
                throw std::runtime_error(
                    "Compared owner reference geometry changed");
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])=="--compare-part-geometry") {
        try {
            using namespace zima;
            std::vector<kernel::BodyResult> first_boundaries,
                second_boundaries;
            const auto first=document::PartDocument::load(
                std::filesystem::u8path(argv[2]),&first_boundaries);
            const auto second=document::PartDocument::load(
                std::filesystem::u8path(argv[3]),&second_boundaries);
            if(first_boundaries.empty()||second_boundaries.empty())
                throw std::runtime_error(
                    "Compared Parts require calculated results");
            const auto local=[](const auto& document,const auto& boundaries)
                    ->const kernel::BodyResult& {
                const auto body=document.body_history.active_body_id();
                const auto found=boundaries.back().body_boundaries.find(body);
                return found==boundaries.back().body_boundaries.end()
                    ?boundaries.back():found->second.back();
            };
            const auto& first_result=local(first,first_boundaries);
            const auto& second_result=local(second,second_boundaries);
            const auto first_shape=kernel::read_calculated_body_shape(
                first_result);
            const auto second_shape=kernel::read_calculated_body_shape(
                second_result);
            if(first_shape.IsNull()||second_shape.IsNull())
                throw std::runtime_error(
                    "Compared Parts have no final native shape");
            const auto difference=[](const TopoDS_Shape& source,
                    const TopoDS_Shape& tool) {
                BRepAlgoAPI_Cut cut(source,tool);
                cut.SetFuzzyValue(1.0e-7);cut.Build();
                if(!cut.IsDone()||cut.Shape().IsNull())return -1.;
                GProp_GProps properties;
                BRepGProp::VolumeProperties(cut.Shape(),properties);
                return std::abs(properties.Mass());
            };
            const auto count=[](const TopoDS_Shape& shape,
                    TopAbs_ShapeEnum kind) {
                TopTools_IndexedMapOfShape values;
                TopExp::MapShapes(shape,kind,values);
                return values.Extent();
            };
            const double first_minus_second=difference(
                first_shape,second_shape);
            const double second_minus_first=difference(
                second_shape,first_shape);
            std::cout<<std::setprecision(17)
                <<"compare_part_geometry first_minus_second="
                <<first_minus_second
                <<" second_minus_first="<<second_minus_first
                <<" first_valid="
                <<BRepCheck_Analyzer(first_shape,true,true,false).IsValid()
                <<" second_valid="
                <<BRepCheck_Analyzer(second_shape,true,true,false).IsValid()
                <<" first_faces="<<count(first_shape,TopAbs_FACE)
                <<" second_faces="<<count(second_shape,TopAbs_FACE)
                <<" first_edges="<<count(first_shape,TopAbs_EDGE)
                <<" second_edges="<<count(second_shape,TopAbs_EDGE)
                <<" first_vertices="<<count(first_shape,TopAbs_VERTEX)
                <<" second_vertices="<<count(second_shape,TopAbs_VERTEX)
                <<'\n';
            if(first_minus_second<0||second_minus_first<0||
                    first_minus_second>1.0e-6||second_minus_first>1.0e-6)
                throw std::runtime_error(
                    "Compared Parts have different final material");
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==6&&std::string_view(argv[1])=="--edit-thin-profile-probe") {
        try {
            return edit_thin_profile_probe(std::filesystem::u8path(argv[2]),
                std::filesystem::u8path(argv[3]),argv[4],
                std::stod(argv[5]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])=="--compare-part-results") {
        try {
            std::vector<zima::kernel::BodyResult> first_boundaries,
                second_boundaries;
            const auto first=zima::document::PartDocument::load(
                std::filesystem::u8path(argv[2]),&first_boundaries);
            const auto second=zima::document::PartDocument::load(
                std::filesystem::u8path(argv[3]),&second_boundaries);
            const auto definition_diff=nlohmann::json::diff(
                first.serialized(),second.serialized());
            if(!definition_diff.empty())
                std::cout<<"definition_diff="
                    <<definition_diff.dump().substr(0,8000)<<'\n';
            if(first_boundaries.size()!=second_boundaries.size()||
                    first_boundaries.empty())
                throw std::runtime_error(
                    "Compared Parts do not have the same calculated history size");
            const auto& first_result=first_boundaries.back();
            const auto& second_result=second_boundaries.back();
            const auto first_packet=
                zima::document::serialize_body_result(first_result);
            const auto second_packet=
                zima::document::serialize_body_result(second_result);
            const auto reference_key=[](const auto& reference) {
                return reference.owner_id+'\x1f'+reference.semantic_key+'\x1f'+
                    reference.instance_path;
            };
            const auto references=[&](const auto& result) {
                std::set<std::string> values;
                const auto append=[&](const auto& range) {
                    for(const auto& value:range)
                        values.insert(reference_key(value.reference));
                };
                for(const auto& value:result.mesh.triangle_references)
                    values.insert(reference_key(value));
                append(result.mesh.edges);append(result.mesh.points);
                append(result.mesh.axes);
                for(const auto& value:result.kernel_faces)
                    values.insert(reference_key(value.reference));
                for(const auto& value:result.kernel_edges)
                    values.insert(reference_key(value.reference));
                for(const auto& value:result.kernel_vertices)
                    values.insert(reference_key(value.reference));
                return values;
            };
            const auto first_references=references(first_result);
            const auto second_references=references(second_result);
            std::vector<std::string> missing_references,added_references;
            std::ranges::set_difference(first_references,second_references,
                std::back_inserter(missing_references));
            std::ranges::set_difference(second_references,first_references,
                std::back_inserter(added_references));
            const auto packet_diff=nlohmann::json::diff(
                first_packet,second_packet);
            std::cout<<std::setprecision(17)
                <<"compare_part_results boundaries="<<first_boundaries.size()
                <<" definition_equal="<<definition_diff.empty()
                <<" packet_equal="<<(first_packet==second_packet)
                <<" packet_diff_entries="<<packet_diff.size()
                <<" reference_sets_equal="
                <<(first_references==second_references)
                <<" first_references="<<first_references.size()
                <<" second_references="<<second_references.size()
                <<" missing_references="<<missing_references.size()
                <<" added_references="<<added_references.size()
                <<" first_volume="<<first_result.volume
                <<" second_volume="<<second_result.volume
                <<" volume_difference="
                <<std::abs(first_result.volume-second_result.volume)
                <<" first_area="<<first_result.surface_area
                <<" second_area="<<second_result.surface_area
                <<" area_difference="
                <<std::abs(first_result.surface_area-second_result.surface_area)
                <<" first_vertices="<<first_result.mesh.vertices.size()
                <<" second_vertices="<<second_result.mesh.vertices.size()
                <<" first_triangles="<<first_result.mesh.triangles.size()/3
                <<" second_triangles="<<second_result.mesh.triangles.size()/3
                <<" first_edges="<<first_result.mesh.edges.size()
                <<" second_edges="<<second_result.mesh.edges.size()<<'\n';
            if(first_packet!=second_packet)
                std::cout<<"packet_diff_excerpt="
                    <<packet_diff.dump().substr(0,8000)<<'\n';
            const auto print_references=[](std::string_view label,
                    const std::vector<std::string>& values) {
                for(std::size_t index=0;index<std::min<std::size_t>(
                        values.size(),20);++index)
                    std::cout<<label<<'['<<index<<"]="
                        <<values[index].substr(0,1000)<<'\n';
            };
            const auto print_reference_owners=[](std::string_view label,
                    const std::vector<std::string>& values) {
                std::map<std::string,std::size_t> owners;
                for(const auto& value:values) {
                    const auto separator=value.find('\x1f');
                    ++owners[value.substr(0,separator)];
                }
                for(const auto& [owner,count]:owners)
                    std::cout<<label<<"_owner="<<owner
                        <<" count="<<count<<'\n';
            };
            print_reference_owners("missing",missing_references);
            print_reference_owners("added",added_references);
            print_references("missing",missing_references);
            print_references("added",added_references);
            if(first_references!=second_references)
                throw std::runtime_error(
                    "Compared Parts changed stable reference identity sets");
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&std::string_view(argv[1])=="--aggregate-property-demand-probe") {
        try {
            return aggregate_property_demand_probe(std::stoull(argv[2]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&std::string_view(argv[1])=="--history-cursor-probe") {
        try {
            return history_cursor_probe(std::filesystem::u8path(argv[2]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==5&&std::string_view(argv[1])==
            "--history-cursor-boundary-probe") {
        try {
            return history_cursor_boundary_probe(
                std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]),
                std::filesystem::u8path(argv[4]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==5&&std::string_view(argv[1])==
            "--history-cursor-invalidation-probe") {
        try {
            return history_cursor_invalidation_probe(
                std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]),
                std::filesystem::u8path(argv[4]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])=="--edge-route-probe") {
        try {
            return edge_route_probe(std::filesystem::u8path(argv[2]),argv[3]);
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&std::string_view(argv[1])=="--calculation-plan-probe") {
        try {
            return calculation_plan_probe(std::filesystem::u8path(argv[2]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==5&&std::string_view(argv[1])=="--sequential-range-probe") {
        try {
            return sequential_range_probe(std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]),std::stoull(argv[4]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&(std::string_view(argv[1])==
            "--append-last-operation-probe"||
            std::string_view(argv[1])==
            "--append-last-operation-equivalence-probe"||
            std::string_view(argv[1])==
            "--append-redundant-last-operation-equivalence-probe")) {
        try {
            return append_last_operation_probe(
                std::filesystem::u8path(argv[2]),
                std::string_view(argv[1])!=
                    "--append-last-operation-probe",
                std::string_view(argv[1])==
                    "--append-redundant-last-operation-equivalence-probe");
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])==
            "--direct-regeneration-prefix-probe") {
        try {
            return direct_regeneration_prefix_probe(
                std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])==
            "--planar-fillet-equivalence-probe") {
        try {
            return planar_fillet_equivalence_probe(
                std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])==
            "--planar-fillet-batch-probe") {
        try {
            return planar_fillet_batch_probe(
                std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&std::string_view(argv[1])==
            "--primary-region-treatment-probe") {
        try {
            return primary_region_treatment_probe(
                std::filesystem::u8path(argv[2]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if((argc==3||argc==4)&&std::string_view(argv[1])==
            "--direct-exact-equivalence-probe") {
        try {
            return direct_exact_equivalence_probe(
                std::filesystem::u8path(argv[2]),
                argc==4?std::stoull(argv[3]):0);
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==5&&(std::string_view(argv[1])=="--batch-add-probe"||
                  std::string_view(argv[1])=="--batch-boolean-probe"||
                  std::string_view(argv[1])=="--batch-publish-probe"||
                  std::string_view(argv[1])=="--boundary-graph-probe"||
                  std::string_view(argv[1])=="--boundary-treatment-probe"||
                  std::string_view(argv[1])=="--localized-treatment-probe")) {
        try {
            const bool boundary_graph=
                std::string_view(argv[1])=="--boundary-graph-probe"||
                std::string_view(argv[1])=="--boundary-treatment-probe";
            const bool boundary_treatment=
                std::string_view(argv[1])=="--boundary-treatment-probe";
            const bool localized_treatment=
                std::string_view(argv[1])=="--localized-treatment-probe";
            return batch_add_probe(std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]),std::stoull(argv[4]),
                !boundary_graph&&!localized_treatment&&
                    std::string_view(argv[1])!="--batch-publish-probe",
                !boundary_graph&&
                    std::string_view(argv[1])!="--batch-publish-probe",
                {},boundary_graph,boundary_treatment,
                localized_treatment);
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==6&&std::string_view(argv[1])=="--batch-template-probe") {
        try {
            return batch_add_probe(std::filesystem::u8path(argv[2]),
                std::stoull(argv[4]),std::stoull(argv[5]),false,true,
                std::filesystem::u8path(argv[3]));
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&std::string_view(argv[1])==
            "--convert-part-final-cache") {
        try {
            const auto source=std::filesystem::u8path(argv[2]);
            const auto destination=std::filesystem::u8path(argv[3]);
            std::vector<zima::kernel::BodyResult> previous;
            const auto load_start=Clock::now();
            const auto document=zima::document::PartDocument::load(
                source,&previous);
            const auto load_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-load_start).count();
            if(previous.empty())
                throw std::runtime_error(
                    "Converted Part has no reference calculated result");
            const auto reference_shape=
                zima::kernel::read_calculated_body_shape(previous.back());
            TopTools_IndexedMapOfShape reference_faces;
            TopExp::MapShapes(reference_shape,TopAbs_FACE,reference_faces);
            GProp_GProps reference_properties;
            BRepGProp::VolumeProperties(reference_shape,
                reference_properties);
            const auto reference_identity_set=[&](const auto& body) {
                std::set<std::tuple<std::string,std::string,std::string>>
                    result;
                const auto append=[&](const auto& values) {
                    for(const auto& value:values) {
                        const auto& reference=value.reference;
                        if(reference.valid())result.emplace(
                            reference.owner_id,reference.semantic_key,
                            reference.instance_path);
                    }
                };
                append(body.kernel_faces);append(body.kernel_edges);
                append(body.kernel_vertices);
                for(const auto& reference:
                        body.mesh.triangle_references)
                    if(reference.valid())result.emplace(
                        reference.owner_id,reference.semantic_key,
                        reference.instance_path);
                append(body.mesh.edges);append(body.mesh.points);
                return result;
            };
            const auto reference_identities=
                reference_identity_set(previous.back());

            zima::kernel::OcctKernel kernel;
            zima::workspace::PartCalculationPolicy policy;
            const auto calculate_start=Clock::now();
            auto calculated=zima::workspace::calculate_part(
                kernel,document,nullptr,policy);
            const auto calculate_ms=
                std::chrono::duration<double,std::milli>(
                    Clock::now()-calculate_start).count();
            if(calculated.size()!=document.kernel_operations(false,true).
                    size()||calculated.empty()||
               !calculated.back().calculation_errors.empty())
                throw std::runtime_error(
                    "Converted Part did not calculate its complete history");
            const auto converted_shape=
                zima::kernel::read_calculated_body_shape(
                    calculated.back());
            TopTools_IndexedMapOfShape converted_faces;
            TopExp::MapShapes(converted_shape,TopAbs_FACE,converted_faces);
            GProp_GProps converted_properties;
            BRepGProp::VolumeProperties(converted_shape,
                converted_properties);
            const auto volume_delta=std::abs(
                std::abs(reference_properties.Mass())-
                std::abs(converted_properties.Mass()));
            if(converted_faces.Extent()!=reference_faces.Extent()||
               volume_delta>0.05||
               reference_identity_set(calculated.back())!=
                    reference_identities)
                throw std::runtime_error(
                    "Converted Part changed final geometry or reference identity");

            const auto save_start=Clock::now();
            document.save(destination,calculated);
            const auto save_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-save_start).count();
            const auto reopen_start=Clock::now();
            std::vector<zima::kernel::BodyResult> reopened_boundaries;
            const auto reopened=zima::document::PartDocument::load(
                destination,&reopened_boundaries);
            const auto reopen_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-reopen_start).count();
            if(reopened.serialized().dump()!=document.serialized().dump()||
               reopened_boundaries.size()!=calculated.size()||
               reopened_boundaries.empty()||
               reference_identity_set(reopened_boundaries.back())!=
                    reference_identities)
                throw std::runtime_error(
                    "Converted Part did not preserve its definition or identities after reopen");
            const auto reopened_shape=
                zima::kernel::read_calculated_body_shape(
                    reopened_boundaries.back());
            TopTools_IndexedMapOfShape reopened_faces;
            TopExp::MapShapes(reopened_shape,TopAbs_FACE,reopened_faces);
            GProp_GProps reopened_properties;
            BRepGProp::VolumeProperties(reopened_shape,
                reopened_properties);
            const auto reopen_volume_delta=std::abs(
                std::abs(converted_properties.Mass())-
                std::abs(reopened_properties.Mass()));
            if(reopened_faces.Extent()!=converted_faces.Extent()||
               reopen_volume_delta>1.0e-7)
                throw std::runtime_error(
                    "Converted Part changed final geometry after reopen");
            std::cout<<std::setprecision(17)
                <<"load_ms="<<load_ms
                <<" calculate_ms="<<calculate_ms
                <<" save_ms="<<save_ms
                <<" reopen_ms="<<reopen_ms
                <<" bytes="<<std::filesystem::file_size(destination)
                <<" boundaries="<<calculated.size()
                <<" faces="<<converted_faces.Extent()
                <<" volume_delta="<<volume_delta
                <<" reopen_volume_delta="<<reopen_volume_delta
                <<" identities="<<reference_identities.size()<<'\n';
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==4&&(std::string_view(argv[1])=="--resave-part"||
                  std::string_view(argv[1])=="--resave-part-reuse"||
                  std::string_view(argv[1])=="--resave-part-metadata-reuse")) {
        try {
            const bool reuse=std::string_view(argv[1])!="--resave-part";
            const bool metadata_reuse=
                std::string_view(argv[1])=="--resave-part-metadata-reuse";
            const auto source=std::filesystem::u8path(argv[2]);
            const auto destination=std::filesystem::u8path(argv[3]);
            const auto load_start=Clock::now();
            std::vector<zima::kernel::BodyResult> boundaries;
            zima::document::NativeCalculatedCachePtr native_cache;
            auto document=zima::document::PartDocument::load(
                source,&boundaries,reuse?&native_cache:nullptr);
            if(reuse&&!native_cache)
                throw std::runtime_error("Part has no reusable calculated cache");
            if(metadata_reuse)
                document.body_color=document.body_color=="#A1B2C3"
                    ?"#C3B2A1":"#A1B2C3";
            const auto definition=document.serialized().dump();
            const auto load_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-load_start).count();
            const auto save_start=Clock::now();
            const auto written=document.save_with_native_cache(
                destination,boundaries,std::move(native_cache),{},
                metadata_reuse
                    ?zima::document::NativeCalculatedCacheReuse::
                        UnchangedCalculatedPacket
                    :zima::document::NativeCalculatedCacheReuse::
                        ExactDefinition);
            if(reuse&&!written)
                throw std::runtime_error("Part save did not reuse its calculated cache");
            const auto save_ms=std::chrono::duration<double,std::milli>(
                Clock::now()-save_start).count();
            std::vector<zima::kernel::BodyResult> reopened_boundaries;
            const auto reopened=zima::document::PartDocument::load(
                destination,&reopened_boundaries);
            if(reopened.serialized().dump()!=definition||
               reopened_boundaries.size()!=boundaries.size())
                throw std::runtime_error("Resaved Part changed its definition or boundary count");
            for(std::size_t index=0;index<boundaries.size();++index)
                if(zima::document::serialize_body_result(reopened_boundaries[index])!=
                   zima::document::serialize_body_result(boundaries[index]))
                    throw std::runtime_error("Resaved Part changed exact calculated data");
            std::cout<<std::setprecision(17)<<"load_ms="<<load_ms
                <<" save_ms="<<save_ms<<" reused="<<reuse
                <<" metadata="<<metadata_reuse<<" bytes="
                <<std::filesystem::file_size(destination)<<" boundaries="
                <<boundaries.size()<<'\n';
            return 0;
        } catch(const std::exception& error) {
            std::cerr<<error.what()<<'\n';return 1;
        }
    }
    if(argc==3&&(std::string_view(argv[1])=="--load-part"||
                  std::string_view(argv[1])=="--identity-stats-part")) {
        try {
            const auto start=Clock::now();
            std::vector<zima::kernel::BodyResult> boundaries;
            const auto document=zima::document::PartDocument::load(std::filesystem::u8path(argv[2]),&boundaries);
            const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            std::cout<<std::setprecision(17)<<"load_ms="<<elapsed<<" history="<<document.history.size()
                     <<" boundaries="<<boundaries.size()<<'\n';
            if(std::string_view(argv[1])=="--identity-stats-part") {
                if(boundaries.empty())throw std::runtime_error("Part has no calculated boundaries");
                const auto packet=zima::document::serialize_body_result(boundaries.back());
                std::size_t uses{},bytes{},maximum{};std::string longest;
                std::set<std::string> distinct;
                const auto visit=[&](const auto& self,const nlohmann::json& value)->void {
                    if(value.is_object())for(const auto& [key,child]:value.items()) {
                        if(key=="key"&&child.is_string()) {
                            const auto& text=child.template get_ref<const std::string&>();
                            ++uses;bytes+=text.size();distinct.insert(text);
                            if(text.size()>maximum){maximum=text.size();longest=text;}
                        } else self(self,child);
                    } else if(value.is_array())for(const auto& child:value)self(self,child);
                };
                visit(visit,packet);
                const auto excerpt=[&](bool suffix) {
                    constexpr std::size_t width=240;
                    if(longest.size()<=width)return longest;
                    return suffix?longest.substr(longest.size()-width):longest.substr(0,width);
                };
                std::cout<<"identity_uses="<<uses<<" identity_bytes="<<bytes
                    <<" distinct="<<distinct.size()<<" max="<<maximum
                    <<" longest_prefix="<<excerpt(false)
                    <<" longest_suffix="<<excerpt(true)<<'\n';
                return 0;
            }
            for(const auto& body:boundaries)
                std::cout<<"boundary "<<body.source_fingerprint<<" volume="<<body.volume
                         <<" vertices="<<body.mesh.vertices.size()<<" triangles="<<body.mesh.triangles.size()
                         <<" edges="<<body.mesh.edges.size()<<" archive_bytes="<<body.kernel_shape.get().size()<<'\n';
            return 0;
        } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string_view(argv[1])=="--packet-part") {
        try {
            std::ifstream input(std::filesystem::u8path(argv[2]),std::ios::binary);nlohmann::json packet;input>>packet;
            auto local=zima::document::load_body_result(packet);
            auto document=zima::document::PartDocument::create_default();document.history.clear();document.history_order.clear();
            auto feature=zima::document::PartDocument::create_imported_step_container("captured-diagnostic.step");
            feature.id=local.mesh.original_references.triangle_references.at(0).owner_id;
            feature.feature_parent_id=feature.id;
            feature.container_origin=zima::document::create_container_origin(feature.id);
            feature.imported_step.frozen_brep=std::make_shared<const std::string>(local.kernel_shape);
            feature.imported_step.topology=local.imported_step_topology;
            document.history.push_back(feature);
            zima::document::BodyHistoryGraph graph;
            const auto body_id=zima::document::create_origin_bound_body(graph,document.document_id,"Imported stress Body");
            graph.insert({zima::document::PartHistoryKind::Feature,feature.id});document.set_body_history(graph);
            const auto branch=document.body_document(body_id).kernel_operations();
            local.source_fingerprint=zima::kernel::history_fingerprint(branch,branch.size());
            auto result=local;result.body_boundaries[body_id]={local};result.body_outputs[body_id]=local;
            const auto operations=document.kernel_operations();
            result.source_fingerprint=zima::kernel::history_fingerprint(operations,operations.size());
            document.save(std::filesystem::u8path(argv[3]),{result});
            std::cout<<"Saved diagnostic Part from the captured native packet; Body="<<body_id<<" feature="<<feature.id<<'\n';return 0;
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(argc==3&&std::string_view(argv[1])=="--packet-picking") {
        try {
            std::ifstream input(std::filesystem::u8path(argv[2]),std::ios::binary);
            nlohmann::json packet;input>>packet;
            const auto body=zima::document::load_body_result(packet);
            const auto& mesh=body.mesh;
            zima::kernel::ViewerMesh references;
            references.vertices=mesh.original_references.vertices;references.triangles=mesh.original_references.triangles;
            references.triangle_references=mesh.original_references.triangle_references;references.edges=mesh.original_references.edges;
            references.points=mesh.original_references.points;references.axes=mesh.original_references.axes;
            const auto build_start=Clock::now();
            const zima::viewer::PickingIndex display_index(mesh),reference_index(references);
            const double build_ms=std::chrono::duration<double,std::milli>(Clock::now()-build_start).count();
            auto lo=mesh.vertices.at(0),hi=lo;
            for(auto v:mesh.vertices){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
            double old_ms=0,new_ms=0;std::size_t tested=0,culled=0,hits=0;
            for(int axis=0;axis<3;++axis)for(int a=0;a<5;++a)for(int b=0;b<5;++b) {
                const std::array low{lo.x,lo.y,lo.z},high{hi.x,hi.y,hi.z};
                std::array<double,3> origin{},direction{};direction[axis]=1;
                origin[axis]=low[axis]-std::max(1.,high[axis]-low[axis]);
                origin[(axis+1)%3]=low[(axis+1)%3]+(high[(axis+1)%3]-low[(axis+1)%3])*(a+.5)/5;
                origin[(axis+2)%3]=low[(axis+2)%3]+(high[(axis+2)%3]-low[(axis+2)%3])*(b+.5)/5;
                const zima::kernel::Vec3 o{origin[0],origin[1],origin[2]},d{direction[0],direction[1],direction[2]};
                const double tolerance=std::max(1e-6,std::hypot(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z)*1e-4);
                const auto start=Clock::now();
                const auto expected=zima::viewer::ordered_viewer_candidates(mesh,references,o,d,tolerance,false,false,false);
                const auto middle=Clock::now();
                const auto actual=zima::viewer::ordered_viewer_candidates(mesh,references,o,d,tolerance,false,false,false,&display_index,&reference_index);
                const auto end=Clock::now();
                if(expected!=actual)throw std::runtime_error("Accelerated picker changed complete ordered candidates");
                old_ms+=std::chrono::duration<double,std::milli>(middle-start).count();new_ms+=std::chrono::duration<double,std::milli>(end-middle).count();
                tested+=mesh.triangles.size()/3+references.triangles.size()/3;
                culled+=display_index.triangles(o,d).size()+reference_index.triangles(o,d).size();hits+=actual.size();
            }
            std::cout<<"Packet picking: 75 rays, candidates="<<hits<<" triangles="<<mesh.triangles.size()/3
                <<" reference_triangles="<<references.triangles.size()/3<<" build_ms="<<build_ms
                <<" exhaustive_mean_ms="<<old_ms/75<<" indexed_mean_ms="<<new_ms/75
                <<" exact_triangle_slots="<<culled<<" exhaustive_triangle_slots="<<tested<<"; complete ordered candidates equal\n";
            return 0;
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if (argc == 3 && std::string_view(argv[1]) == "--step") {
        try {
            return step_benchmark(argv[2]);
        } catch (const std::exception& error) {
            std::cerr << "STEP benchmark failed: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    constexpr std::size_t part_features = 24;
    constexpr std::size_t assembly_components = 256;
    constexpr std::size_t nested_levels = 3;
    constexpr int repetitions = 3;

    zima::kernel::OcctKernel kernel;
    const auto part = part_fixture(part_features);
    const auto operations = part.kernel_operations();
    const auto part_boundaries = kernel.evaluate_history(operations);
    if (part_boundaries.size() != part_features) {
        std::cerr << "part fixture calculation produced unexpected boundaries\n";
        return EXIT_FAILURE;
    }
    const auto reference_count = [](const auto& geometry) {
        return geometry.triangle_references.size() + geometry.edges.size() +
            geometry.points.size() + geometry.axes.size();
    };
    std::size_t stored_reference_items = 0;
    std::size_t stored_kernel_bytes = 0;
    for (const auto& boundary : part_boundaries) {
        stored_reference_items += reference_count(
            boundary.mesh.original_references);
        stored_kernel_bytes += boundary.kernel_shape.size();
    }
    std::unordered_map<std::string, std::size_t> owner_reference_items;
    const auto& final_references =
        part_boundaries.back().mesh.original_references;
    for (const auto& reference : final_references.triangle_references) {
        ++owner_reference_items[reference.owner_id];
    }
    for (const auto& edge : final_references.edges) {
        ++owner_reference_items[edge.reference.owner_id];
    }
    for (const auto& point : final_references.points) {
        ++owner_reference_items[point.reference.owner_id];
    }
    for (const auto& axis : final_references.axes) {
        ++owner_reference_items[axis.reference.owner_id];
    }
    std::size_t cumulative_reference_items = 0;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        cumulative_reference_items += owner_reference_items[operations[index].owner_id] *
            (operations.size() - index);
    }
    auto changed_part = part;
    zima::test::profile_dimension(changed_part,changed_part.history.back(),2) = 24.0;
    const auto changed_operations = changed_part.kernel_operations();
    const auto part_incremental_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history_incremental(
            changed_operations, part_boundaries);
        if (result.size() != part_features ||
            std::abs(result.back().volume - 93120.0) > 1.0e-6) std::abort();
    }, repetitions);
    const auto part_cold_incremental_ms = milliseconds([&] {
        zima::kernel::OcctKernel cold_kernel;
        const auto result = cold_kernel.evaluate_history_incremental(
            changed_operations, part_boundaries);
        if (result.size() != part_features ||
            std::abs(result.back().volume - 93120.0) > 1.0e-6) std::abort();
    }, repetitions);

    auto fillet_base_operations = operations;
    for (std::size_t index = 0; index < fillet_base_operations.size(); ++index) {
        auto request=zima::test::rectangular_request(20,20,20);
        for(auto& point:std::get<zima::kernel::ExtrusionRequest::PolygonProfile>(request.outer_profile).vertices)point.x+=static_cast<double>(index)*30.;
        fillet_base_operations[index].primitive=std::move(request);
    }
    const auto fillet_base_boundaries =
        kernel.evaluate_history(fillet_base_operations);
    const auto fillet_edge = std::find_if(
        fillet_base_boundaries.back().mesh.original_references.edges.begin(),
        fillet_base_boundaries.back().mesh.original_references.edges.end(),
        [](const auto& edge) {
            return edge.reference.owner_id == "benchmark-box-23";
        });
    if (fillet_edge ==
        fillet_base_boundaries.back().mesh.original_references.edges.end()) {
        std::cerr << "part fixture has no stable edge for Fillet benchmark\n";
        return EXIT_FAILURE;
    }
    auto fillet_operations = fillet_base_operations;
    fillet_operations.push_back({"benchmark-fillet",
        zima::kernel::FilletRequest{{fillet_edge->reference},
            1.0},
        zima::kernel::BooleanOperation::Add});
    const auto fillet_boundaries = kernel.evaluate_history(fillet_operations);
    auto changed_fillet_operations = fillet_operations;
    std::get<zima::kernel::FilletRequest>(
        changed_fillet_operations.back().primitive).radius_start = 1.5;
    const auto fillet_incremental_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history_incremental(
            changed_fillet_operations, fillet_boundaries);
        if (result.size() != fillet_operations.size() ||
            result.back().volume <= 0.0) std::abort();
    }, repetitions);
    const auto fillet_cold_incremental_ms = milliseconds([&] {
        zima::kernel::OcctKernel cold_kernel;
        const auto result = cold_kernel.evaluate_history_incremental(
            changed_fillet_operations, fillet_boundaries);
        if (result.size() != fillet_operations.size() ||
            result.back().volume <= 0.0) std::abort();
    }, repetitions);

    zima::assembly::AssemblyDocument scene_fixture =
        zima::assembly::AssemblyDocument::create_default();
    for (std::size_t index = 0; index < assembly_components; ++index) {
        auto occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
            "benchmark-component-" + std::to_string(index), "benchmark-part",
            {}, zima::test::profile_body(kernel,{10.0, 10.0, 10.0}));
        occurrence.placement.x = static_cast<double>(index % 32) * 14.0;
        occurrence.placement.y = static_cast<double>(index / 32) * 14.0;
        scene_fixture.components.push_back(std::move(occurrence));
    }
    const auto scene = scene_fixture.build_scene();
    const auto scene_ms = milliseconds([&] {
        const auto built = scene_fixture.build_scene();
        if (built.triangles.size() != scene.triangles.size()) std::abort();
    }, repetitions);
    std::size_t picking_candidates = 0;
    const auto picking_ms = milliseconds([&] {
        const auto candidates = zima::viewer::ordered_viewer_candidates(
            scene, {0.0, 0.0, -100.0}, {0.0, 0.0, 1.0}, 0.5);
        if (candidates.empty()) std::abort();
        picking_candidates = candidates.size();
    }, repetitions);

    zima::workspace::Workspace workspace;
    auto source_part = part_fixture(4);
    source_part.document_id = "benchmark-source-part";
    const auto source_boundaries = kernel.evaluate_history(
        source_part.kernel_operations());
    workspace.add_part(std::move(source_part), source_boundaries);
    auto nested = zima::assembly::AssemblyDocument::create_default();
    nested.document_id = "benchmark-nested-assembly";
    workspace.add_assembly(std::move(nested));
    static_cast<void>(workspace.insert_open_part(
        "benchmark-nested-assembly", "benchmark-source-part", "nested-part"));
    for (std::size_t level = 1; level < nested_levels; ++level) {
        auto parent = zima::assembly::AssemblyDocument::create_default();
        parent.document_id = "benchmark-parent-" + std::to_string(level);
        workspace.add_assembly(std::move(parent));
        static_cast<void>(workspace.insert_open_assembly(
            "benchmark-parent-" + std::to_string(level),
            level == 1 ? "benchmark-nested-assembly"
                       : "benchmark-parent-" + std::to_string(level - 1),
            "nested-assembly"));
    }
    const std::string top_id =
        "benchmark-parent-" + std::to_string(nested_levels - 1);
    const auto regeneration_ms = milliseconds([&] {
        workspace.regenerate_assembly_from_open_dependencies(top_id);
    }, repetitions);

    const auto sketch_solve_ms = [&](std::size_t branches) {
        const auto fixture = solver_fixture(branches);
        return milliseconds([&] {
            auto candidate = fixture;
            const auto solved = candidate.solve();
            if (solved.status != zima::sketcher::SolveStatus::Solved ||
                solved.remaining_degrees_of_freedom != 0 ||
                solved.maximum_residual > 1.0e-7) std::abort();
        }, repetitions);
    };
    const auto sketch_10_ms = sketch_solve_ms(10);
    const auto sketch_40_ms = sketch_solve_ms(40);
    const auto sketch_100_ms = sketch_solve_ms(100);
    const auto connected_solve_ms = [&](std::size_t segments) {
        const auto fixture = connected_solver_fixture(segments);
        return milliseconds([&] {
            auto candidate = fixture;
            const auto solved = candidate.solve();
            if (solved.status != zima::sketcher::SolveStatus::Solved ||
                solved.remaining_degrees_of_freedom != 0 ||
                solved.maximum_residual > 1.0e-7) std::abort();
        }, repetitions);
    };
    const auto chain_10_ms = connected_solve_ms(10);
    const auto chain_40_ms = connected_solve_ms(40);
    const auto chain_100_ms = connected_solve_ms(100);
    const auto chain_250_ms = connected_solve_ms(250);
    const auto graph_fixture = solver_fixture(1000);
    std::size_t graph_components{};
    const auto graph_1000_ms = milliseconds([&] {
        graph_components = mobility_component_count(graph_fixture);
        if (graph_components != 1000) std::abort();
    }, 25);
    auto drag_fixture = solver_fixture(100);
    if (drag_fixture.solve().status != zima::sketcher::SolveStatus::Solved) {
        std::abort();
    }
    const auto drag_point_id = drag_fixture.dimensions.back().second_point_id;
    drag_fixture.dimensions.pop_back();
    const auto drag_100_ms = milliseconds([&] {
        auto candidate = drag_fixture;
        const auto* point = candidate.find_point(drag_point_id);
        if (point == nullptr ||
            !candidate.move_point(drag_point_id, point->x + 1.0, point->y)) {
            std::abort();
        }
    }, repetitions);
    const auto dimension_fixture = solver_fixture(100);
    const auto dimension_edit_100_ms = milliseconds([&] {
        auto candidate = dimension_fixture;
        if (!candidate.set_dimension_value("perf-d:99", 6.0)) std::abort();
    }, repetitions);
    const auto constraint_fixture = solver_fixture(100);
    const auto constraint_remove_100_ms = milliseconds([&] {
        auto candidate = constraint_fixture;
        candidate.remove_constraint("perf-h:99");
    }, repetitions);
    auto cached_solve_fixture = solver_fixture(100);
    if (cached_solve_fixture.solve().status !=
        zima::sketcher::SolveStatus::Solved) std::abort();
    const auto cached_full_solve_100_ms = milliseconds([&] {
        auto candidate = cached_solve_fixture;
        if (candidate.solve().status != zima::sketcher::SolveStatus::Solved) {
            std::abort();
        }
    }, repetitions);
    auto constraint_add_fixture = solver_fixture(100);
    const auto added_segment_id = constraint_add_fixture.segments.back().id;
    constraint_add_fixture.constraints.pop_back();
    if (constraint_add_fixture.solve().remaining_degrees_of_freedom != 1) {
        std::abort();
    }
    const auto constraint_add_100_ms = milliseconds([&] {
        auto candidate = constraint_add_fixture;
        static_cast<void>(candidate.add_segment_constraint(
            added_segment_id, zima::sketcher::ConstraintKind::Horizontal));
    }, repetitions);
    auto large_drag_fixture = solver_fixture(1000);
    if (large_drag_fixture.solve().status != zima::sketcher::SolveStatus::Solved) {
        std::abort();
    }
    const auto large_drag_point_id =
        large_drag_fixture.dimensions.back().second_point_id;
    large_drag_fixture.dimensions.pop_back();
    const auto drag_1000_ms = milliseconds([&] {
        auto candidate = large_drag_fixture;
        const auto* point = candidate.find_point(large_drag_point_id);
        if (point == nullptr ||
            !candidate.move_point(
                large_drag_point_id, point->x + 1.0, point->y)) std::abort();
    }, 1);
    const auto large_dimension_fixture = solver_fixture(1000);
    const auto dimension_edit_1000_ms = milliseconds([&] {
        auto candidate = large_dimension_fixture;
        if (!candidate.set_dimension_value("perf-d:999", 6.0)) std::abort();
    }, 1);
    const auto copy_1000_ms = milliseconds([&] {
        auto candidate = large_dimension_fixture;
        if (candidate.points.size() != 2000) std::abort();
    }, 3);
    const auto validate_1000_ms = milliseconds([&] {
        large_dimension_fixture.validate();
    }, 3);

    std::cout << std::fixed << std::setprecision(3)
              << "ZIMA_PERF_V1 repetitions=" << repetitions << '\n'
              << "part_history features=" << part_features
              << " boundaries=" << part_boundaries.size()
              << " volume=" << part_boundaries.back().volume
              << " mean_ms=" << milliseconds([&] {
                     const auto result = kernel.evaluate_history(operations);
                     if (result.size() != part_features) std::abort();
                 }, repetitions) << '\n'
              << "part_incremental changed_index=" << (part_features - 1)
              << " reused_boundaries=" << (part_features - 1)
              << " mean_ms=" << part_incremental_ms << '\n'
              << "part_cold_incremental changed_index=" << (part_features - 1)
              << " persisted_brep=final_only"
              << " mean_ms=" << part_cold_incremental_ms << '\n'
              << "part_reference_cache stored_items=" << stored_reference_items
              << " cumulative_items=" << cumulative_reference_items
              << " kernel_bytes=" << stored_kernel_bytes << '\n'
              << "part_fillet_incremental changed_index=" << part_features
              << " reused_boundaries=" << part_features
              << " mean_ms=" << fillet_incremental_ms << '\n'
              << "part_fillet_cold_incremental changed_index=" << part_features
              << " mean_ms=" << fillet_cold_incremental_ms << '\n'
              << "assembly_scene components=" << assembly_components
              << " triangles=" << scene.triangles.size()
              << " mean_ms=" << scene_ms << '\n'
              << "assembly_picking components=" << assembly_components
              << " candidates=" << picking_candidates
              << " mean_ms=" << picking_ms << '\n'
              << "nested_regeneration levels=" << nested_levels
              << " mean_ms=" << regeneration_ms << '\n'
              << "sketch_solver branches=10 variables=20 equations=20"
              << " mean_ms=" << sketch_10_ms << '\n'
              << "sketch_solver branches=40 variables=80 equations=80"
              << " mean_ms=" << sketch_40_ms << '\n'
              << "sketch_solver branches=100 variables=200 equations=200"
              << " mean_ms=" << sketch_100_ms << '\n'
              << "sketch_solver_chain segments=10 components=1 mean_ms="
              << chain_10_ms << '\n'
              << "sketch_solver_chain segments=40 components=1 mean_ms="
              << chain_40_ms << '\n'
              << "sketch_solver_chain segments=100 components=1 mean_ms="
              << chain_100_ms << '\n'
              << "sketch_solver_chain segments=250 components=1 mean_ms="
              << chain_250_ms << '\n'
              << "sketch_mobility_graph points=2000 components="
              << graph_components << " mean_ms=" << graph_1000_ms << '\n'
              << "sketch_interaction operation=drag branches=100 mean_ms="
              << drag_100_ms << '\n'
              << "sketch_interaction operation=dimension_edit branches=100 mean_ms="
              << dimension_edit_100_ms << '\n'
              << "sketch_interaction operation=constraint_remove branches=100 mean_ms="
              << constraint_remove_100_ms << '\n'
              << "sketch_interaction operation=cached_full_solve branches=100 mean_ms="
              << cached_full_solve_100_ms << '\n'
              << "sketch_interaction operation=constraint_add branches=100 mean_ms="
              << constraint_add_100_ms << '\n'
              << "sketch_interaction operation=drag branches=1000 mean_ms="
              << drag_1000_ms << '\n'
              << "sketch_interaction operation=dimension_edit branches=1000 mean_ms="
              << dimension_edit_1000_ms << '\n'
              << "sketch_interaction operation=copy branches=1000 mean_ms="
              << copy_1000_ms << '\n'
              << "sketch_interaction operation=validate branches=1000 mean_ms="
              << validate_1000_ms << '\n';
    return EXIT_SUCCESS;
}
