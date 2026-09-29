#include <zima/document/boundary_surface.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/surface_results.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/viewer/picking.hpp>
#include <filesystem>
#include <cstdlib>
#include <zima/workspace/boundary_surface_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/history_policy.hpp>
#include <iostream>
using namespace zima;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
kernel::BoundarySurfaceRequest rectangle(double height=0){
    kernel::BoundarySurfaceRequest r;r.region_id="region";
    const std::array<kernel::Vec3,4> p{{{0,0,0},{100,0,0},{100,80,height},{0,80,0}}};
    for(unsigned i=0;i<4;++i){auto& b=r.boundaries[i];r.source_owners[i]="source"+std::to_string(i);
        b.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::LineCurve{p[i],p[(i+1)%4]}}};
        b.outer_edge_source_ids={"curve"};b.outer_vertex_source_ids={"a"};b.open_profile_end_id="b";b.direction={0,0,1};}
    return r;
}
int main(){try{
    kernel::OcctKernel k;
    const auto calculate=[&](const auto& r){return k.evaluate_history({{"boundary",r}}).back();};
    auto r=rectangle();auto flat=calculate(r);
    require(flat.calculation_errors.empty()&&std::abs(flat.surface_area-8000)<0.01&&std::abs(flat.volume)<1e-9,"Planar surface area or volume failed");
    require(kernel::has_surface_results(flat.mesh),"Surface classification lost");
    require(flat.mesh.original_references.edges.size()==4,"Boundary edge identities missing");
    require(flat.mesh.original_references.points.size()==4,"Corner identities missing");
    const auto verify_cut_points=[](const kernel::BodyResult& result,const std::string& owner) {
        std::size_t count=0;
        for(const auto& point:result.mesh.points) {
            if(point.reference.owner_id!=owner||!point.reference.semantic_key.starts_with("boolean:subtract:vertex"))continue;
            ++count;
            const auto found=std::ranges::find_if(result.mesh.original_references.points,
                [&](const auto& reference){return reference.reference==point.reference;});
            require(found!=result.mesh.original_references.points.end(),"Cut endpoint missing from persisted reference geometry");
            require(std::hypot(found->position.x-point.position.x,found->position.y-point.position.y,found->position.z-point.position.z)<1e-9,
                "Cut endpoint reference has the wrong position");
            const auto candidates=viewer::ordered_viewer_candidates(result.mesh,
                {point.position.x,point.position.y,point.position.z+1000},{0,0,-1},1e-5);
            require(std::ranges::any_of(candidates,[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Vertex&&
                candidate.owner_id==point.reference.owner_id&&candidate.semantic_key==point.reference.semantic_key&&
                candidate.geometry==viewer::CandidateGeometry::OriginalReference;}),"Common picker omitted a persisted cut endpoint");
        }
        require(count>=2,"Cut produced no independently identified endpoints");
    };
    kernel::ExtrusionRequest tool;
    tool.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{40,-10,-10},{60,-10,-10},{60,90,-10},{40,90,-10}}};
    tool.direction={0,0,20};tool.profile_region_id="cut-region";
    tool.outer_edge_source_ids={"e0","e1","e2","e3"};tool.outer_vertex_source_ids={"p0","p1","p2","p3"};
    const auto cut=k.evaluate_history({{"boundary",rectangle()},{"cut",tool,kernel::BooleanOperation::Subtract}});
    require(cut.back().calculation_errors.empty()&&std::abs(cut.back().surface_area-6400)<.01,"Boundary strip cut failed");
    verify_cut_points(cut.back(),"cut");
    auto wider=tool;std::get<kernel::ExtrusionRequest::PolygonProfile>(wider.outer_profile).vertices[1].x=65;
    std::get<kernel::ExtrusionRequest::PolygonProfile>(wider.outer_profile).vertices[2].x=65;
    const auto changed_cut=k.evaluate_history({{"boundary",rectangle()},{"cut",wider,kernel::BooleanOperation::Subtract}});
    verify_cut_points(changed_cut.back(),"cut");
    for(const auto& point:cut.back().mesh.points)if(point.reference.owner_id=="cut")
        require(std::ranges::any_of(changed_cut.back().mesh.points,[&](const auto& changed){return changed.reference==point.reference;}),
            "Changing cut width changed endpoint ancestry");
    if(const auto* file=std::getenv("ZIMA_VERIFY_BOUNDARY_SOURCE")) {
        const auto example=document::PartDocument::load(std::filesystem::path(file));
        const auto operations=example.kernel_operations();
        const auto calculated=k.evaluate_history(operations);
        require(!calculated.empty()&&calculated.back().calculation_errors.empty(),"User cut example failed calculation");
        verify_cut_points(calculated.back(),operations.back().owner_id);
        std::size_t curves=0;
        for(const auto& edge:calculated.back().mesh.original_references.edges) {
            if(edge.reference.owner_id!=operations.back().owner_id||!edge.reference.semantic_key.starts_with("boolean:subtract:"))continue;
            require(edge.exact_spline.has_value(),"Cut curve lacks exact geometry");
            ++curves;
            const auto& spline=*edge.exact_spline;
            const auto p=kernel::bspline_value(spline,.5);
            document::Placement placed;placed.x=p.x;placed.y=p.y;placed.z=p.z;
            placed.references={{{},edge.reference.owner_id,edge.reference.semantic_key}};
            require(document::resolve_placement(placed,calculated.back().mesh.original_references),"Cut curve placement failed");
            require(std::hypot(placed.x-p.x,placed.y-p.y,placed.z-p.z)<1e-6,"Cut curve forced placement to its endpoint");
        }
        require(curves>0,"User cut has no intersection curves");
        const auto copy=std::filesystem::temp_directory_path()/"zima-boundary-cut-reference-test.prtz";
        example.save(copy,calculated);
        std::vector<kernel::BodyResult> restored;
        static_cast<void>(document::PartDocument::load(copy,&restored));
        require(!restored.empty(),"User cut cache was not restored");
        verify_cut_points(restored.back(),operations.back().owner_id);
        std::filesystem::remove(copy);
        std::cout<<"User cut endpoints: calculated and native save/reopen passed\n";
    }
    const auto first_corner=std::ranges::find_if(flat.mesh.original_references.points,[](const auto& p){return p.reference.semantic_key=="boundary:source:source0:vertex:from:a";});
    require(first_corner!=flat.mesh.original_references.points.end()&&std::hypot(first_corner->position.x,first_corner->position.y)<1e-9,"Source corner identity was attached to the wrong endpoint");
    auto cycled=rectangle();std::rotate(cycled.boundaries.begin(),cycled.boundaries.begin()+1,cycled.boundaries.end());
    std::rotate(cycled.source_owners.begin(),cycled.source_owners.begin()+1,cycled.source_owners.end());
    const auto cycled_result=calculate(cycled);
    for(const auto& corner:flat.mesh.original_references.points) {
        const auto found=std::ranges::find_if(cycled_result.mesh.original_references.points,[&](const auto& p){return p.reference.semantic_key==corner.reference.semantic_key;});
        require(found!=cycled_result.mesh.original_references.points.end()&&std::hypot(found->position.x-corner.position.x,found->position.y-corner.position.y)<1e-9,"Changing the first boundary changed corner identity");
    }
    auto warped=calculate(rectangle(20));require(warped.calculation_errors.empty()&&warped.surface_area>8000,"Spatial patch failed");
    std::get<kernel::ExtrusionRequest::CurvedProfile>(r.boundaries[0].outer_profile).curves[0]=kernel::ExtrusionRequest::ArcCurve{{0,0,0},{50,-15,10},{100,0,0}};
    const auto curved=calculate(r);require(curved.calculation_errors.empty()&&curved.surface_area>8000,"Exact arc boundary failed");
    kernel::ExtrusionRequest::BSplineCurve rational;
    rational.start={0,0,0};rational.end={100,0,0};
    rational.control_points={{0,0,0},{20,-20,15},{80,-20,15},{100,0,0}};rational.weights={1,2,2,1};
    std::get<kernel::ExtrusionRequest::CurvedProfile>(r.boundaries[0].outer_profile).curves[0]=rational;
    const auto weighted=calculate(r);require(weighted.calculation_errors.empty()&&weighted.surface_area>8000,"Rational B-spline boundary failed");
    auto broken=rectangle();std::get<kernel::ExtrusionRequest::CurvedProfile>(broken.boundaries[0].outer_profile).curves[0]=kernel::ExtrusionRequest::LineCurve{{5,0,0},{100,0,0}};
    bool rejected=false;
    try { rejected=!calculate(broken).calculation_errors.empty(); } catch(const std::exception&) { rejected=true; }
    require(rejected,"Open perimeter accepted");
    auto reversed=rectangle();
    auto& reverse_curve=std::get<kernel::ExtrusionRequest::LineCurve>(std::get<kernel::ExtrusionRequest::CurvedProfile>(reversed.boundaries[2].outer_profile).curves[0]);
    std::swap(reverse_curve.start,reverse_curve.end);
    require(std::abs(calculate(reversed).surface_area-8000)<.01,"Automatic chain reversal changed the patch");
    auto crossing=rectangle();const std::array<kernel::Vec3,4> crossed{{{0,0,0},{100,80,0},{0,80,0},{100,0,0}}};
    for(int i=0;i<4;++i)std::get<kernel::ExtrusionRequest::CurvedProfile>(crossing.boundaries[i].outer_profile).curves[0]=kernel::ExtrusionRequest::LineCurve{crossed[i],crossed[(i+1)%4]};
    rejected=false;try{rejected=!calculate(crossing).calculation_errors.empty();}catch(const std::exception&){rejected=true;}
    require(rejected,"Crossing boundaries were accepted");
    auto part=document::PartDocument::create_default();part.history.clear();part.sketches.clear();part.history_order.clear();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Boundary"));
    auto feature=document::create_boundary_surface();
    auto sketch=sketcher::Sketch::create_default();auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
    const std::array<std::array<double,4>,4> lines{{{0,0,100,0},{100,0,100,80},{100,80,0,80},{0,80,0,0}}};
    for(unsigned i=0;i<4;++i)feature.boundary_surface.boundaries[i]={container.id,sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3])};
    part.history={container,feature};part.sketches={sketch};graph.insert({document::PartHistoryKind::Feature,container.id});graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    const auto output=k.evaluate_history(part.kernel_operations());require(output.back().calculation_errors.empty(),"Native Sketch boundary failed");
    auto current=part;auto* current_source=current.find_container(container.id);
    current_source->feature_kind=document::FeatureKind::Feature;
    current_source->feature.type=document::FeatureType::Sketch;
    current_source->feature.sketch_id=sketch.id;
    require(document::boundary_surface_source_allowed(current,feature.id,{container.id,{}}),"Current Sketch Feature was rejected as a whole boundary");
    const auto current_output=k.evaluate_history(current.kernel_operations());
    require(current_output.back().calculation_errors.empty()&&std::abs(current_output.back().surface_area-output.back().surface_area)<1e-7,
        "Current Sketch Feature boundaries changed the calculated surface");
    current_source->feature.type=document::FeatureType::Modeling;
    require(!document::boundary_surface_source_allowed(current,feature.id,{container.id,{}}),"Solid modeling Feature was offered as a Sketch boundary");
    const auto saved=document::PartDocument::from_serialized(part.serialized());
    require(saved.find_container(feature.id)->boundary_surface==feature.boundary_surface,"Boundary persistence failed");
    auto changed=part;changed.sketches.front().points.front().x+=2;
    require(kernel::history_fingerprint(changed.kernel_operations(),changed.kernel_operations().size())!=kernel::history_fingerprint(part.kernel_operations(),part.kernel_operations().size()),"Source edit did not invalidate surface cache");
    const auto temporary=std::filesystem::temp_directory_path()/"zima-boundary-surface-test.prtz";
    part.save(temporary,output);std::vector<kernel::BodyResult> persisted;
    const auto reopened=document::PartDocument::load(temporary,&persisted);std::filesystem::remove(temporary);
    require(reopened.find_container(feature.id)->boundary_surface==feature.boundary_surface&&!persisted.empty(),"Native file lost definition or calculated geometry");
    require(std::abs(persisted.back().surface_area-8000)<.01,"Native file lost surface area");
    const auto dependencies=workspace::part_history_dependencies(part);
    require(!dependencies.empty(),"Boundary source dependency missing");
    auto input=part;input.history.pop_back();document::BodyHistoryGraph input_graph;
    static_cast<void>(input_graph.create_body("Boundary"));input_graph.insert({document::PartHistoryKind::Feature,container.id});input.set_body_history(input_graph);
    workspace::Workspace live;live.add_part(input,workspace::calculate_part_with_resolved_references(k,input));live.activate(input.document_id);
    require(workspace::commit_boundary_surface(live,k,input.document_id,feature),"Boundary transaction did not commit");
    require(!workspace::commit_boundary_surface(live,k,input.document_id,feature),"Unchanged OK created a transaction");
    require(workspace::step_part_document_history(live,input.document_id,false),"Surface Undo failed");
    require(!live.open_part(input.document_id)->session.document().find_container(feature.id),"Undo retained surface");
    require(workspace::step_part_document_history(live,input.document_id,true),"Surface Redo failed");
    require(live.open_part(input.document_id)->session.document().find_container(feature.id),"Redo lost surface");
    const auto before=live.open_part(input.document_id)->session.document().serialized();auto invalid=feature;invalid.boundary_surface.boundaries[3]={};
    rejected=false;try{static_cast<void>(workspace::commit_boundary_surface(live,k,input.document_id,invalid));}catch(const std::exception&){rejected=true;}
    require(rejected&&before==live.open_part(input.document_id)->session.document().serialized(),"Invalid edit changed the document");
    auto spatial=document::PartDocument::create_default();spatial.history.clear();spatial.sketches.clear();spatial.constructions.clear();
    document::BodyHistoryGraph curve_graph;static_cast<void>(curve_graph.create_body("3D boundaries"));
    auto patch=document::create_boundary_surface();
    const std::array<kernel::Vec3,4> corners{{{0,0,0},{100,0,0},{100,80,20},{0,80,0}}};
    for(int i=0;i<4;++i) {
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        curve.curve_points.clear();curve.curve_type=i==0?document::Curve3DType::InterpolatingSpline:document::Curve3DType::Polyline;
        for(const auto p:{corners[i],corners[(i+1)%4]}){auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);}
        if(i==0){auto middle=document::create_owned_point(curve.id);middle.origin={50,-15,10};curve.curve_points.insert(curve.curve_points.begin()+1,middle);}
        patch.boundary_surface.boundaries[i]={curve.id,{}};curve_graph.insert({document::PartHistoryKind::Construction,curve.id});spatial.constructions.push_back(curve);
    }
    curve_graph.insert({document::PartHistoryKind::Feature,patch.id});spatial.history={patch};spatial.set_body_history(curve_graph);spatial.resolve_constructions();
    const auto spatial_result=k.evaluate_history(spatial.kernel_operations());
    require(spatial_result.back().calculation_errors.empty()&&spatial_result.back().surface_area>8000,"3D Curve boundary surface failed");
    require(document::PartDocument::from_serialized(spatial.serialized()).find_container(patch.id)->boundary_surface==patch.boundary_surface,"3D curve references did not round trip");
    const auto original_start=document::boundary_surface_request(spatial,patch).boundaries[0].outer_vertex_source_ids.front();
    auto refined=spatial;auto& first_curve=refined.constructions.front();auto inserted=document::create_owned_point(first_curve.id);
    inserted.origin={25,-10,5};first_curve.curve_points.insert(first_curve.curve_points.begin()+1,inserted);
    require(document::boundary_surface_request(refined,patch).boundaries[0].outer_vertex_source_ids.front()==original_start,"Inserting a 3D Curve point changed its unchanged endpoint identity");
    if(const auto* example=std::getenv("ZIMA_BOUNDARY_EXAMPLE"))spatial.save(std::filesystem::path(example),spatial_result);
    std::cout<<"Boundary surfaces: plane, spatial patch, arc, gaps, identity, Sketch input, persistence and cache passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
