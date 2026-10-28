#include "profile_solid_fixture.hpp"
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/workspace/workspace.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <fstream>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools_ShapeSet.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>

namespace {

using Clock = std::chrono::steady_clock;

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
        bool verify_equivalence=true) {
    using namespace zima;
    std::vector<kernel::BodyResult> previous;
    const auto document=document::PartDocument::load(source,&previous);
    kernel::AggregatePropertyScope aggregate_properties(
        kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(false);
    const auto operations=document.kernel_operations(false,true);
    if(first==0||first>last||last>=operations.size()||last>=previous.size())
        throw std::runtime_error("Batch Add range is outside the calculated history");
    const auto boolean_operation=operations[first].operation;
    for(std::size_t index=first;index<=last;++index)
        if(operations[index].suppressed||
           operations[index].operation!=boolean_operation)
            throw std::runtime_error("Batch range mixes Boolean operations or contains a suppressed operation");
    const auto body_id=document.body_history.active_body_id();
    const auto local_body=[&](std::size_t boundary)->const kernel::BodyResult& {
        const auto& value=previous.at(boundary);
        const auto found=value.body_boundaries.find(body_id);
        return found==value.body_boundaries.end()?value:found->second.back();
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
        return result;
    };
    auto input_shape=bound_face_shape(local_body(first-1));
    if(input_shape.root.IsNull())
        throw std::runtime_error("Batch Add input has no bound native shape");
    auto source_faces=std::move(input_shape.faces);
    auto source_edges=std::move(input_shape.edges);
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
            <<operations[index].owner_id<<'\n';
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
            tools.push_back(std::move(shape.root));
            source_faces.insert(source_faces.end(),
                std::make_move_iterator(shape.faces.begin()),
                std::make_move_iterator(shape.faces.end()));
            source_edges.insert(source_edges.end(),
                std::make_move_iterator(shape.edges.begin()),
                std::make_move_iterator(shape.edges.end()));
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
    TopTools_ListOfShape arguments,tool_list;
    arguments.Append(input_shape.root);
    for(const auto& tool:tools)tool_list.Append(tool);
    BRepAlgoAPI_BooleanOperation fuse;
    fuse.SetOperation(boolean_operation==kernel::BooleanOperation::Add
        ?BOPAlgo_FUSE:BOPAlgo_CUT);
    fuse.SetArguments(arguments);fuse.SetTools(tool_list);
    fuse.SetRunParallel(true);fuse.SetToFillHistory(true);
    double tolerance=1.0e-7;
    for(std::size_t index=first;index<=last;++index)
        tolerance=std::max(tolerance,operations[index].boolean_tolerance);
    fuse.SetFuzzyValue(tolerance);
    const auto fuse_start=Clock::now();fuse.Build();
    const auto fuse_ms=std::chrono::duration<double,std::milli>(
        Clock::now()-fuse_start).count();
    if(!fuse.IsDone()||fuse.Shape().IsNull()||
       !BRepCheck_Analyzer(fuse.Shape()).IsValid())
        throw std::runtime_error("Batch Add did not produce a valid shape");
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
    std::set<std::string> expected_face_references;
    for(const auto& binding:local_body(last).kernel_faces)
        expected_face_references.insert(reference_key(binding.reference));
    std::vector<std::string> retained_face_references;
    std::ranges::set_intersection(expected_face_references,
        propagated_face_references,
        std::back_inserter(retained_face_references));
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
        <<" tool_ms="<<tool_ms<<" fuse_ms="<<fuse_ms
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
    std::optional<std::size_t> current_batch;
    for(std::size_t index=0;index<operations.size();++index) {
        const auto& operation=operations[index];
        const auto eligibility=additive_batch_eligibility(operation);
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
    const auto* calculated_allocation=
        state->session.calculated_boundaries().data();
    const std::set<std::size_t> positions{
        0,std::min<std::size_t>(1,entry_count),entry_count/4,
        entry_count/2,(entry_count*3)/4,entry_count};
    double maximum_commit{},maximum_scene{};
    for(const auto position:positions) {
        const auto revision=state->session.revision();
        const auto commit_start=Clock::now();
        const bool changed=workspace::set_body_history_cursor(
            live,document_id,position,body_id);
        const auto commit_ms=std::chrono::duration<double,std::milli>(
            Clock::now()-commit_start).count();
        if(!changed||state->session.revision()!=revision+1)
            throw std::runtime_error(
                "History cursor move did not create its metadata transaction");
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
        if(state->session.calculated_boundaries().data()!=calculated_allocation)
            throw std::runtime_error(
                "History cursor move copied the immutable calculated cache");
        std::cout<<std::setprecision(17)
            <<"history_cursor position="<<position
            <<" entries="<<entry_count
            <<" commit_ms="<<commit_ms
            <<" scene_ms="<<scene_ms
            <<" vertices="<<mesh.vertices.size()
            <<" triangles="<<mesh.triangles.size()/3
            <<" edges="<<mesh.edges.size()<<'\n';
    }
    if(entry_count!=state->session.document().body_history.find(body_id)->cursor)
        static_cast<void>(workspace::set_body_history_cursor(
            live,document_id,entry_count,body_id));
    std::cout<<std::setprecision(17)
        <<"history_cursor_summary maximum_commit_ms="<<maximum_commit
        <<" maximum_scene_ms="<<maximum_scene
        <<" occt_operations=0\n";
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

int main(int argc, char** argv) {
    if(argc==3&&std::string_view(argv[1])=="--history-cursor-probe") {
        try {
            return history_cursor_probe(std::filesystem::u8path(argv[2]));
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
    if(argc==5&&(std::string_view(argv[1])=="--batch-add-probe"||
                  std::string_view(argv[1])=="--batch-boolean-probe"||
                  std::string_view(argv[1])=="--batch-publish-probe")) {
        try {
            return batch_add_probe(std::filesystem::u8path(argv[2]),
                std::stoull(argv[3]),std::stoull(argv[4]),
                std::string_view(argv[1])!="--batch-publish-probe",
                std::string_view(argv[1])!="--batch-publish-probe");
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
