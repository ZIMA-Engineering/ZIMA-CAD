#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <numbers>
#include <set>

namespace {
namespace k=zima::kernel;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
k::ExtrusionRequest box() {
    k::ExtrusionRequest request;
    request.outer_profile=k::ExtrusionRequest::PolygonProfile{{{-10,-10,5},{10,-10,5},{10,10,5},{-10,10,5}}};
    request.outer_edge_source_ids={"bottom","right","top","left"};
    request.outer_vertex_source_ids={"a","b","c","d"};
    request.profile_region_id="box-region";request.outer_boundary_id="rectangle";request.direction={0,0,6};
    return request;
}
k::ProjectionRequest projection(k::FaceReference target,k::ProjectionResult output) {
    k::ProjectionRequest request;request.output=output;request.sketch_id="projection-sketch";
    request.sketch_origin={0,0,20};request.sketch_normal={0,0,1};request.direction={0,0,-1};request.targets={target};
    request.tolerance=1e-7;
    auto& profile=request.profiles.emplace_back();
    profile.outer_profile=k::ExtrusionRequest::CircleProfile{{0,0,20},4};
    profile.outer_edge_source_ids={"circle"};profile.profile_region_id="disk";
    profile.outer_boundary_id="circle-boundary";profile.direction={0,0,1};profile.surface_result=true;
    if(output==k::ProjectionResult::Surface)request.regions.push_back({target,{0,0,11},{}});
    return request;
}
}
int main(){try {
    k::OcctKernel kernel;
    const k::HistoryOperation base{"base",box()};
    const auto initial=kernel.evaluate_history({base}).back();
    k::FaceReference target;
    for(const auto& face:initial.mesh.original_references.triangle_references)
        if(face.owner_id=="base"&&face.semantic_key.starts_with("end:from:"))target=face;
    require(target.valid(),"Missing authored top face");
    {
        const k::HistoryReferenceIdentity absent{"base","generated:absent-fragment",{}};
        k::HistoryOutputPolicy policy;policy.final_boundary_first=true;
        policy.reference_output_owners["base"].insert(absent);
        std::vector<k::HistoryOperation> operations{base,
            {"earlier-projection",projection(target,k::ProjectionResult::Curves)},
            {"edited-projection",projection(target,k::ProjectionResult::Curves)}};
        auto saved=kernel.evaluate_history_recovering(operations,{},policy);
        require(saved.front().history_reference_coverage.contains(absent),
            "Missing-fragment fixture has no fresh absence proof");
        // Native reopen discards only the disposable coverage proof.
        for(auto& boundary:saved)boundary.history_reference_coverage.clear();
        const auto* retained=saved[1].kernel_shape.get().data();
        auto& edited=std::get<k::ProjectionRequest>(operations.back().primitive);
        std::get<k::ExtrusionRequest::CircleProfile>(edited.profiles[0].outer_profile).radius=3.;
        k::OcctKernel cold;
        const auto result=cold.evaluate_history_recovering(operations,saved,policy);
        require(result.back().calculation_errors.empty()&&
            result.front().history_reference_coverage.contains(absent),
            "Cold suffix edit did not restore exact absent-reference coverage");
        require(result[1].kernel_shape.get().data()==retained,
            "Recovering an earlier reference packet recalculated unchanged later geometry");
        k::OcctKernel independent;
        const auto expected=independent.evaluate_history_recovering(operations,{},policy);
        require(std::ranges::equal(result.back().mesh.edges,expected.back().mesh.edges,
                [](const auto& a,const auto& b) {
                    return a.points==b.points&&a.reference==b.reference&&a.exact_spline==b.exact_spline&&
                        a.color==b.color&&a.surface_result==b.surface_result;
                })&&result.back().mesh.vertices==expected.back().mesh.vertices&&
            result.back().mesh.triangles==expected.back().mesh.triangles&&
            result.back().mesh.triangle_references==expected.back().mesh.triangle_references,
            "Reference-only recovery changed projected geometry or stable identities");
        std::cout<<"Cold absent-reference recovery preserved unchanged history and exact Projection output\n";
    }
    for(const auto output:{k::ProjectionResult::Curves,k::ProjectionResult::Split,k::ProjectionResult::Surface}) {
        const auto request=projection(target,output);
        const std::vector<k::HistoryOperation> operations{base,{"projection",request}};
        const auto result=kernel.evaluate_history(operations).back();
        require(result.calculation_errors.empty(),"Projection history calculation failed");
        require(std::abs(result.volume-2400)<1e-5,"Projection changed material volume");
        const auto shape=k::read_calculated_body_shape(result);
        require(BRepCheck_Analyzer(shape).IsValid(),"Projection produced invalid topology");
        if(output==k::ProjectionResult::Curves) {
            bool found=false;
            for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="projection") {
                require(edge.reference.semantic_key.starts_with("projection:curve:"),"Projected curve lost semantic ancestry");found=true;
            }
            require(found,"Projected curves were not published as native references");
            require(result.surface_area==initial.surface_area,"Curve output changed surface area");
        } else if(output==k::ProjectionResult::Split) {
            bool parent=false;
            for(const auto& face:result.mesh.triangle_references)if(face.owner_id=="projection") {
                const auto ancestry=k::boolean_face_parent(face.semantic_key);
                require(ancestry&&ancestry->first==target.owner_id&&ancestry->second==target.semantic_key,
                    "Split lost the original placement face identity");parent=true;
            }
            require(parent,"Split did not publish divided faces");
            for(bool reverse:{false,true}) {
                const auto before=k::read_calculated_body_shape(initial);
                BRepAlgoAPI_Cut difference(reverse?before:shape,reverse?shape:before);
                require(difference.IsDone(),"Independent material difference failed");
                GProp_GProps mass;BRepGProp::VolumeProperties(difference.Shape(),mass);
                require(std::abs(mass.Mass())<1e-5,"Split changed occupied material space");
            }
        } else {
            std::string retained;
            for(const auto& face:result.mesh.original_references.triangle_references)
                if(face.owner_id=="projection"&&face.surface_result)retained=face.semantic_key;
            require(!retained.empty(),"Selected independent surface was not published");
            auto edited=request;edited.regions[0].expected_region_key=retained;edited.regions[0].seed={100,100,100};
            const auto stable=kernel.evaluate_history({base,{"projection",edited}}).back();
            require(stable.calculation_errors.empty(),"Persisted region incorrectly followed stale pick coordinates");
            require(std::abs(stable.surface_area-result.surface_area)<1e-5,"Retained region changed after seed moved");
            k::OcctKernel fresh;
            const auto cold=fresh.evaluate_history({base,{"projection",edited}}).back();
            require(cold.calculation_errors.empty()&&std::abs(cold.surface_area-result.surface_area)<1e-5,"Cold region reconstruction changed output");
        }
        std::cout<<"PASS history output "<<static_cast<int>(output)<<'\n';
    }
    // A downstream operation consumes the exact curve identity again after
    // successive source edits, including reconstruction by a fresh kernel.
    k::BoundarySurfaceRequest plane;plane.region_id="projection-plane";
    const std::array<k::Vec3,4> corners{{{-10,-10,11},{10,-10,11},{10,10,11},{-10,10,11}}};
    for(std::size_t i=0;i<4;++i) {
        auto& edge=plane.boundaries[i];plane.source_owners[i]="plane-source-"+std::to_string(i);
        edge.outer_profile=k::ExtrusionRequest::CurvedProfile{{k::ExtrusionRequest::LineCurve{corners[i],corners[(i+1)%4]}}};
        edge.outer_edge_source_ids={"edge"};edge.outer_vertex_source_ids={"start"};edge.open_profile_end_id="end";edge.direction={0,0,1};
    }
    const auto sheet=kernel.evaluate_history({{"plane",plane}}).back();
    k::FaceReference plane_face;
    for(const auto& face:sheet.mesh.triangle_references)if(face.owner_id=="plane")plane_face=face;
    require(plane_face.valid(),"Projection trimming fixture has no native plane");
    auto cut_curve=projection(plane_face,k::ProjectionResult::Curves);
    const auto projected=kernel.evaluate_history({{"plane",plane},{"cut-curve",cut_curve}}).back();
    k::SurfaceTrimRequest trim;trim.target=plane_face;trim.seed={0,0,11};
    for(const auto& edge:projected.mesh.original_references.edges)if(edge.reference.owner_id=="cut-curve")trim.tools.push_back({edge.reference,false});
    require(trim.tools.size()==1,"Projection trimming fixture lost its circle identity");
    for(double radius:{3.,5.,4.}) {
        std::get<k::ExtrusionRequest::CircleProfile>(cut_curve.profiles[0].outer_profile).radius=radius;
        k::OcctKernel cold;
        const auto trimmed=cold.evaluate_history({{"plane",plane},{"cut-curve",cut_curve},{"trim",trim}}).back();
        require(trimmed.calculation_errors.empty()&&std::abs(trimmed.surface_area-std::numbers::pi*radius*radius)<1e-5,
            "Surface Trim lost the native projected curve after source editing");
    }
    std::cout<<"Native projected curve references and repeated downstream Surface Trim passed\n";
    // Nested loops retain an annular region by boundary ancestry. Adjacent
    // selected regions must remain independently selectable for thickening.
    auto nested=projection(target,k::ProjectionResult::Surface);
    auto inner=nested.profiles.front();
    std::get<k::ExtrusionRequest::CircleProfile>(inner.outer_profile).radius=2.;
    inner.outer_edge_source_ids={"inner-circle"};inner.outer_boundary_id="inner-boundary";
    nested.profiles.push_back(inner);nested.regions[0].seed={3,0,11};
    const auto annular=kernel.evaluate_history({base,{"nested",nested}}).back();
    require(annular.calculation_errors.empty(),"Nested Projection region failed");
    require(std::abs(annular.surface_area-initial.surface_area-12*std::numbers::pi)<1e-5,"Nested region lost its inner boundary");
    k::FaceReference annular_face;
    for(const auto& face:annular.mesh.triangle_references)if(face.owner_id=="nested"&&face.surface_result)annular_face=face;
    require(annular_face.valid(),"Nested selected region has no native reference");
    k::SurfaceThickenRequest thicken{annular_face,2.};
    const auto thick=kernel.evaluate_history({base,{"nested",nested},{"thicken",thicken}}).back();
    require(thick.calculation_errors.empty(),"Surface Thicken cannot consume Projection region");
    require(thick.volume>2400+23*std::numbers::pi&&thick.volume<2400+25*std::numbers::pi,"Thickening Projection did not preserve annular material");
    nested.regions.push_back({target,{0,0,11},{}});
    const auto adjacent=kernel.evaluate_history({base,{"nested",nested}}).back();
    require(adjacent.calculation_errors.empty(),"Adjacent retained Projection regions failed");
    require(adjacent.resolved_projection_regions.size()==2&&adjacent.resolved_projection_regions[0].boundary_key!=adjacent.resolved_projection_regions[1].boundary_key,"Adjacent regions lost distinct identity");
    require(std::abs(adjacent.surface_area-initial.surface_area-16*std::numbers::pi)<1e-5,"Adjacent regions double-counted or omitted area");
    const auto adjacent_thick=kernel.evaluate_history({base,{"nested",nested},{"thicken",thicken}}).back();
    require(adjacent_thick.calculation_errors.empty(),"Adjacent regions made shared boundary references ambiguous");
    auto lost=nested;lost.regions.resize(1);lost.regions[0].expected_region_key=adjacent.resolved_projection_regions[0].boundary_key;
    lost.profiles.pop_back();
    bool rejected=false;try {rejected=!kernel.evaluate_history({base,{"nested",lost}}).back().calculation_errors.empty();}catch(const std::exception&){rejected=true;}
    require(rejected,"Lost region silently selected a replacement patch");
    auto split=projection(target,k::ProjectionResult::Split);
    k::ShellRequest shell;shell.thickness=0.;
    const auto skin=kernel.evaluate_history({base,{"split",split},{"shell",shell}}).back();
    require(skin.calculation_errors.empty()&&std::abs(skin.surface_area-initial.surface_area)<1e-5,"Shell cannot consume partitioned material");
    // Direction references use native directed straight edges, including
    // orientation; non-straight references must not act as a sampled chord.
    k::EdgeReference axis;
    for(const auto& edge:initial.mesh.original_references.edges)if(edge.points.size()>=2&&
        std::abs(edge.points.back().z-edge.points.front().z)>5.9)axis=edge.reference;
    require(axis.valid(),"No native straight direction support");
    auto directed=projection(target,k::ProjectionResult::Curves);directed.direction_reference=axis;
    bool direction_ok=false;
    for(bool reverse:{false,true}) {directed.reverse=reverse;try {const auto result=kernel.evaluate_history({base,{"directed",directed}}).back();direction_ok|=result.calculation_errors.empty();}catch(const std::exception&) {}}
    require(direction_ok,"Native directed straight edge cannot drive Projection");
    const auto divided=kernel.evaluate_history({base,{"split",split}}).back();
    std::set<std::string> tested;std::optional<k::OrderedUpToSource> external;
    for(const auto& face:divided.mesh.triangle_references)if(face.owner_id=="split"&&tested.insert(face.semantic_key).second) {
        auto captured=kernel.capture_reference_face(divided,face);
        kernel.ensure_body_properties(captured);
        if(std::abs(captured.surface_area-16*std::numbers::pi)<1e-5)external=k::OrderedUpToSource{std::move(captured),face,{}};
    }
    require(external.has_value(),"Cannot capture exact bounded split face");
    auto external_face=external->reference;external_face.instance_path="external-part";
    auto external_projection=projection(external_face,k::ProjectionResult::Curves);
    external_projection.external_targets.push_back({external_face,std::make_shared<const k::OrderedUpToSource>(*external)});
    std::get<k::ExtrusionRequest::CircleProfile>(external_projection.profiles[0].outer_profile).radius=3;
    require(kernel.evaluate_history({{"external-projection",external_projection}}).back().calculation_errors.empty(),"Explicit external bounded face failed");
    std::get<k::ExtrusionRequest::CircleProfile>(external_projection.profiles[0].outer_profile).radius=6;
    rejected=false;try {rejected=!kernel.evaluate_history({{"external-projection",external_projection}}).back().calculation_errors.empty();}catch(const std::exception&) {rejected=true;}
    require(rejected,"External bounded face incorrectly expanded to its original untrimmed ancestor");
    std::cout<<"Nested/adjacent regions, lost-region rejection, Surface Thicken, Shell and native direction passed\n";
    std::cout<<"Explicit external bounded face and rejected untrimmed-ancestor fallback passed\n";
    std::cout<<"Projection native history contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
